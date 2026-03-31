#include "telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-12;

double clampDouble(double value, double minValue, double maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

std::size_t nextPowerOfTwo(std::size_t value) {
    std::size_t out = 1;
    while (out < value) {
        out <<= 1U;
    }
    return out;
}

}  // namespace

AudioTelemetry::AudioTelemetry(int sampleRate, std::size_t windowSize, std::size_t hopSize)
    : sampleRate_(std::max(1, sampleRate)),
      windowSize_(std::max<std::size_t>(256, windowSize)),
      hopSize_(std::max<std::size_t>(1, hopSize)),
      spectralSize_(nextPowerOfTwo(std::max<std::size_t>(256, windowSize_ / 8))),
      spectralBins_(std::min<std::size_t>(64, spectralSize_ / 2)),
      spectralStride_(std::max<std::size_t>(1, windowSize_ / std::max<std::size_t>(1, spectralSize_))),
      ring_(windowSize_, 0.0),
      orderedWindow_(),
      spectralWindow_(),
      hannWindow_(),
      cosTable_(),
      sinTable_(),
      binFreqsHz_(),
      diffsScratch_(),
      spectralUpdateStride_(4),
      spectralUpdateCounter_(0),
      writePos_(0),
      filled_(0),
      samplesSinceUpdate_(0),
      metrics_() {
    if (hopSize_ > windowSize_) {
        hopSize_ = windowSize_;
    }

    if (spectralSize_ < 64) {
        spectralSize_ = 64;
    }
    if (spectralBins_ < 4) {
        spectralBins_ = 4;
    }
    spectralUpdateStride_ = std::max<std::size_t>(1, hopSize_ / 128);

    orderedWindow_.reserve(windowSize_);
    spectralWindow_.resize(spectralSize_, 0.0);
    hannWindow_.resize(spectralSize_, 0.0);
    diffsScratch_.reserve(windowSize_ > 1 ? windowSize_ - 1 : 1);

    initSpectralTables();
}

void AudioTelemetry::pushSample(double sample, std::uint64_t sampleIndex) {
    ring_[writePos_] = sample;
    writePos_ = (writePos_ + 1) % windowSize_;

    if (filled_ < windowSize_) {
        ++filled_;
    }

    ++samplesSinceUpdate_;
    if (filled_ == windowSize_ && samplesSinceUpdate_ >= hopSize_) {
        computeMetrics(sampleIndex);
        samplesSinceUpdate_ = 0;
    }
}

const TelemetryMetrics& AudioTelemetry::metrics() const {
    return metrics_;
}

void AudioTelemetry::copyWindow(std::vector<double>& out) const {
    out.clear();
    out.reserve(windowSize_);
    for (std::size_t i = 0; i < windowSize_; ++i) {
        const std::size_t idx = (writePos_ + i) % windowSize_;
        out.push_back(ring_[idx]);
    }
}

void AudioTelemetry::initSpectralTables() {
    const std::size_t n = spectralSize_;
    if (n == 0) {
        return;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const double phase = (2.0 * kPi * static_cast<double>(i)) /
                             static_cast<double>(std::max<std::size_t>(1, n - 1));
        hannWindow_[i] = 0.5 - 0.5 * std::cos(phase);
    }

    cosTable_.resize(spectralBins_ * n, 0.0);
    sinTable_.resize(spectralBins_ * n, 0.0);
    binFreqsHz_.resize(spectralBins_, 0.0);

    for (std::size_t band = 0; band < spectralBins_; ++band) {
        const std::size_t bin = (band + 1) * (n / 2) / spectralBins_;
        const double freqHz = (static_cast<double>(bin) * static_cast<double>(sampleRate_)) /
                              static_cast<double>(n);
        binFreqsHz_[band] = freqHz;

        const std::size_t base = band * n;
        for (std::size_t i = 0; i < n; ++i) {
            const double angle = (2.0 * kPi * static_cast<double>(bin) * static_cast<double>(i)) /
                                 static_cast<double>(n);
            cosTable_[base + i] = std::cos(angle);
            sinTable_[base + i] = std::sin(angle);
        }
    }
}

void AudioTelemetry::computeMetrics(std::uint64_t sampleIndex) {
    copyWindow(orderedWindow_);
    const std::vector<double>& window = orderedWindow_;
    const std::size_t n = window.size();
    if (n < 2) {
        return;
    }

    double energySum = 0.0;
    std::size_t zcrCrossings = 0;
    double diffSum = 0.0;
    diffsScratch_.clear();
    diffsScratch_.reserve(n - 1);

    for (std::size_t i = 0; i < n; ++i) {
        const double s = window[i];
        energySum += s * s;

        if (i > 0) {
            const double prev = window[i - 1];
            if ((prev >= 0.0 && s < 0.0) || (prev < 0.0 && s >= 0.0)) {
                ++zcrCrossings;
            }

            const double diff = std::fabs(s - prev);
            diffsScratch_.push_back(diff);
            diffSum += diff;
        }
    }

    const double rms = std::sqrt(energySum / static_cast<double>(n));
    const double zcr = static_cast<double>(zcrCrossings) / static_cast<double>(n - 1);

    const double meanDiff = diffSum / static_cast<double>(std::max<std::size_t>(1, diffsScratch_.size()));
    const double transientThreshold = meanDiff * 2.4;
    std::size_t transientCount = 0;
    for (double diff : diffsScratch_) {
        if (diff > transientThreshold) {
            ++transientCount;
        }
    }
    const double transientDensity = static_cast<double>(transientCount) /
                                    static_cast<double>(std::max<std::size_t>(1, diffsScratch_.size()));

    ++spectralUpdateCounter_;
    if (spectralUpdateCounter_ >= spectralUpdateStride_ || !metrics_.valid) {
        spectralUpdateCounter_ = 0;

        for (std::size_t i = 0; i < spectralSize_; ++i) {
            const std::size_t src = std::min<std::size_t>(n - 1, i * spectralStride_);
            spectralWindow_[i] = window[src] * hannWindow_[i];
        }

        double magnitudeSum = 0.0;
        double weightedFreqSum = 0.0;
        double logMagnitudeSum = 0.0;

        for (std::size_t band = 0; band < spectralBins_; ++band) {
            const std::size_t base = band * spectralSize_;
            double re = 0.0;
            double im = 0.0;
            for (std::size_t i = 0; i < spectralSize_; ++i) {
                const double s = spectralWindow_[i];
                re += s * cosTable_[base + i];
                im -= s * sinTable_[base + i];
            }

            const double magnitude = std::sqrt(re * re + im * im) + kEpsilon;
            const double freqHz = binFreqsHz_[band];

            magnitudeSum += magnitude;
            weightedFreqSum += magnitude * freqHz;
            logMagnitudeSum += std::log(magnitude);
        }

        const double centroidHz = magnitudeSum > kEpsilon
                                      ? weightedFreqSum / magnitudeSum
                                      : 0.0;

        const double arithMean = magnitudeSum / static_cast<double>(std::max<std::size_t>(1, spectralBins_));
        const double geoMean = std::exp(logMagnitudeSum / static_cast<double>(std::max<std::size_t>(1, spectralBins_)));
        const double flatness = arithMean > kEpsilon ? geoMean / arithMean : 0.0;

        metrics_.spectralCentroidHz = clampDouble(centroidHz, 0.0, static_cast<double>(sampleRate_) * 0.5);
        metrics_.spectralFlatness = clampDouble(flatness, 0.0, 1.0);
    }

    metrics_.rms = rms;
    metrics_.zeroCrossingRate = clampDouble(zcr, 0.0, 1.0);
    metrics_.transientDensity = clampDouble(transientDensity, 0.0, 1.0);
    metrics_.windowSize = n;
    metrics_.sampleIndex = sampleIndex;
    metrics_.valid = std::isfinite(metrics_.rms) &&
                     std::isfinite(metrics_.spectralCentroidHz) &&
                     std::isfinite(metrics_.spectralFlatness) &&
                     std::isfinite(metrics_.zeroCrossingRate) &&
                     std::isfinite(metrics_.transientDensity);

    if (!metrics_.valid) {
        metrics_ = TelemetryMetrics{};
    }
}
