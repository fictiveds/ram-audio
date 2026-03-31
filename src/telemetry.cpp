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

}  // namespace

AudioTelemetry::AudioTelemetry(int sampleRate, std::size_t windowSize, std::size_t hopSize)
    : sampleRate_(std::max(1, sampleRate)),
      windowSize_(std::max<std::size_t>(256, windowSize)),
      hopSize_(std::max<std::size_t>(1, hopSize)),
      ring_(windowSize_, 0.0),
      writePos_(0),
      filled_(0),
      samplesSinceUpdate_(0),
      metrics_() {
    if (hopSize_ > windowSize_) {
        hopSize_ = windowSize_;
    }
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

std::vector<double> AudioTelemetry::copyWindow() const {
    std::vector<double> window;
    window.reserve(windowSize_);
    for (std::size_t i = 0; i < windowSize_; ++i) {
        const std::size_t idx = (writePos_ + i) % windowSize_;
        window.push_back(ring_[idx]);
    }
    return window;
}

void AudioTelemetry::computeMetrics(std::uint64_t sampleIndex) {
    const std::vector<double> window = copyWindow();
    const std::size_t n = window.size();
    if (n < 2) {
        return;
    }

    double energySum = 0.0;
    std::size_t zcrCrossings = 0;
    double diffSum = 0.0;
    std::vector<double> diffs;
    diffs.reserve(n - 1);

    for (std::size_t i = 0; i < n; ++i) {
        const double s = window[i];
        energySum += s * s;

        if (i > 0) {
            const double prev = window[i - 1];
            if ((prev >= 0.0 && s < 0.0) || (prev < 0.0 && s >= 0.0)) {
                ++zcrCrossings;
            }

            const double diff = std::fabs(s - prev);
            diffs.push_back(diff);
            diffSum += diff;
        }
    }

    const double rms = std::sqrt(energySum / static_cast<double>(n));
    const double zcr = static_cast<double>(zcrCrossings) / static_cast<double>(n - 1);

    const double meanDiff = diffSum / static_cast<double>(std::max<std::size_t>(1, diffs.size()));
    const double transientThreshold = meanDiff * 2.4;
    std::size_t transientCount = 0;
    for (double diff : diffs) {
        if (diff > transientThreshold) {
            ++transientCount;
        }
    }
    const double transientDensity = static_cast<double>(transientCount) / static_cast<double>(std::max<std::size_t>(1, diffs.size()));

    const std::size_t spectrumBands = std::min<std::size_t>(64, n / 2);
    double magnitudeSum = 0.0;
    double weightedFreqSum = 0.0;
    double logMagnitudeSum = 0.0;

    for (std::size_t band = 1; band <= spectrumBands; ++band) {
        const std::size_t bin = (band * (n / 2)) / spectrumBands;
        double re = 0.0;
        double im = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double angle = (2.0 * kPi * static_cast<double>(bin) * static_cast<double>(i)) /
                                 static_cast<double>(n);
            const double s = window[i];
            re += s * std::cos(angle);
            im -= s * std::sin(angle);
        }

        const double magnitude = std::sqrt(re * re + im * im) + kEpsilon;
        const double freqHz = (static_cast<double>(bin) * static_cast<double>(sampleRate_)) /
                              static_cast<double>(n);

        magnitudeSum += magnitude;
        weightedFreqSum += magnitude * freqHz;
        logMagnitudeSum += std::log(magnitude);
    }

    const double centroidHz = magnitudeSum > kEpsilon
                                  ? weightedFreqSum / magnitudeSum
                                  : 0.0;

    const double arithMean = magnitudeSum / static_cast<double>(std::max<std::size_t>(1, spectrumBands));
    const double geoMean = std::exp(logMagnitudeSum / static_cast<double>(std::max<std::size_t>(1, spectrumBands)));
    const double flatness = arithMean > kEpsilon ? geoMean / arithMean : 0.0;

    metrics_.rms = rms;
    metrics_.spectralCentroidHz = clampDouble(centroidHz, 0.0, static_cast<double>(sampleRate_) * 0.5);
    metrics_.spectralFlatness = clampDouble(flatness, 0.0, 1.0);
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
