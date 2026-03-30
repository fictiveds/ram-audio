#include "algorithm_registry.hpp"
#include "common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace ram_audio::algo_detail;

class MicrotonalGlitchGrid final : public IRamAlgorithm {
public:
    MicrotonalGlitchGrid(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          bpm_(randomDouble(rng, 82.0, 172.0)),
          p1_(randomDouble(rng, 0.1, 10.0)),
          samplesToStep_(1),
          rhythmPos_(0),
          useTritave_(randomIntInclusive(rng, 0, 1) == 1),
          targetFreq_(220.0),
          currentFreq_(220.0),
          phase_(0.0),
          fmPhase_(0.0),
          env_(0.0),
          rhythmUnits_{3, 2, 5, 3, 4, 1, 3, 2, 6} {}

    const std::string& id() const override {
        static const std::string kId = "microtonal_glitch_grid";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        if (samplesToStep_ <= 0) {
            triggerStep(sampleIndex, memory, macroMod);
        }
        --samplesToStep_;

        currentFreq_ += (targetFreq_ - currentFreq_) * 0.012;

        phase_ += (2.0 * kPi * currentFreq_) / static_cast<double>(sampleRate_);
        if (phase_ >= 2.0 * kPi) {
            phase_ -= 2.0 * kPi;
        }

        const std::size_t fmIdx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 8192ULL), memorySize_);
        const double fmByte = (static_cast<double>(memory[fmIdx]) - 128.0) / 128.0;
        fmPhase_ += (2.0 * kPi * (currentFreq_ * (1.2 + macroMod * 3.2))) / static_cast<double>(sampleRate_);
        if (fmPhase_ >= 2.0 * kPi) {
            fmPhase_ -= 2.0 * kPi;
        }

        env_ *= (0.9992 - macroMod * 0.0004);
        if (env_ < 0.00005) {
            env_ = 0.00005;
        }

        const double carrier = std::sin(phase_ + std::sin(fmPhase_) * fmByte * (1.2 + macroMod * 5.0));
        const double stepped = std::round(carrier * 8.0) / 8.0;
        const double sub = std::sin(phase_ * 0.5 + fmByte * 0.4);
        const double mix = carrier * 0.64 + stepped * 0.16 + sub * 0.2;

        return std::tanh(mix * 2.2) * env_ * 13500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    void triggerStep(std::uint64_t sampleIndex,
                     const std::vector<std::uint8_t>& memory,
                     double macroMod) {
        static const std::array<double, 15> kRatios = {
            1.0,
            33.0 / 32.0,
            16.0 / 15.0,
            10.0 / 9.0,
            9.0 / 8.0,
            7.0 / 6.0,
            6.0 / 5.0,
            5.0 / 4.0,
            4.0 / 3.0,
            11.0 / 8.0,
            3.0 / 2.0,
            8.0 / 5.0,
            5.0 / 3.0,
            7.0 / 4.0,
            15.0 / 8.0,
        };

        const std::size_t i0 = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 4096ULL), memorySize_);
        const std::size_t i1 = boundedIndex(i0 + 1, memorySize_);
        const std::size_t i2 = boundedIndex(i0 + 2, memorySize_);

        const std::uint8_t b0 = memory[i0];
        const std::uint8_t b1 = memory[i1];
        const std::uint8_t b2 = memory[i2];

        if ((b0 & 0x07U) == 0U) {
            useTritave_ = !useTritave_;
        }

        if (useTritave_) {
            const int degree = b0 % 13;
            const int reg = static_cast<int>(b1 % 5) - 2;
            const double freq = 60.0 * std::pow(3.0, static_cast<double>(degree) / 13.0 + static_cast<double>(reg));
            targetFreq_ = std::clamp(freq, 22.0, 6200.0);
        } else {
            const int degree = b0 % static_cast<int>(kRatios.size());
            const int oct = static_cast<int>(b1 % 5) - 2;
            const double freq = 95.0 * kRatios[degree] * std::pow(2.0, static_cast<double>(oct));
            targetFreq_ = std::clamp(freq, 22.0, 6200.0);
        }

        const int jitter = static_cast<int>(b2 & 0x03U) - 1;
        int units = rhythmUnits_[rhythmPos_ % rhythmUnits_.size()] + jitter;
        if (units < 1) {
            units = 1;
        }

        const double unitSamples = static_cast<double>(sampleRate_) * 60.0 / (std::max(45.0, bpm_) * 24.0);
        samplesToStep_ = std::max(1, static_cast<int>(unitSamples * static_cast<double>(units)));

        bpm_ += static_cast<double>(static_cast<int>(b2 % 7) - 3) * 0.03 + (macroMod - 0.5) * 0.03 * p1_;
        bpm_ = std::clamp(bpm_, 55.0, 215.0);

        ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(b0 + b1 + 1U), memorySize_);
        ++rhythmPos_;
        env_ = 1.0;
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double bpm_;
    double p1_;
    int samplesToStep_;
    std::size_t rhythmPos_;
    bool useTritave_;
    double targetFreq_;
    double currentFreq_;
    double phase_;
    double fmPhase_;
    double env_;
    std::array<int, 9> rhythmUnits_;
};

class PolymeterEuclideanMicro final : public IRamAlgorithm {
public:
    PolymeterEuclideanMicro(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          bpm_(randomDouble(rng, 90.0, 176.0)),
          p1_(randomDouble(rng, 0.1, 10.0)),
          phaseA_(0.0),
          phaseB_(0.0),
          envA_(0.0),
          envB_(0.0),
          freqA_(180.0),
          freqB_(240.0),
          stepA_(0),
          stepB_(0),
          pulsesA_(5),
          pulsesB_(7),
          rotateA_(0),
          rotateB_(0),
          samplesToA_(1),
          samplesToB_(1),
          hold_(0.0) {}

    const std::string& id() const override {
        static const std::string kId = "polymeter_euclidean_micro";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        if (samplesToA_ <= 0) {
            advanceA(sampleIndex, memory, macroMod);
        }
        if (samplesToB_ <= 0) {
            advanceB(sampleIndex, memory, macroMod);
        }
        --samplesToA_;
        --samplesToB_;

        envA_ *= 0.99945;
        envB_ *= 0.99962;
        if (envA_ < 0.00005) {
            envA_ = 0.00005;
        }
        if (envB_ < 0.00005) {
            envB_ = 0.00005;
        }

        phaseA_ += (2.0 * kPi * freqA_) / static_cast<double>(sampleRate_);
        phaseB_ += (2.0 * kPi * freqB_) / static_cast<double>(sampleRate_);
        if (phaseA_ >= 2.0 * kPi) {
            phaseA_ -= 2.0 * kPi;
        }
        if (phaseB_ >= 2.0 * kPi) {
            phaseB_ -= 2.0 * kPi;
        }

        const double a = (std::sin(phaseA_) + 0.3 * std::sin(phaseA_ * 2.7)) * envA_;
        const double sq = std::sin(phaseB_) >= 0.0 ? 1.0 : -1.0;
        const double b = (sq * 0.72 + 0.28 * std::sin(phaseB_ * 0.5)) * envB_;

        if ((sampleIndex & 0x3FULL) == 0ULL) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 2048ULL), memorySize_);
            if ((memory[idx] & 0x03U) == 0U) {
                hold_ = 0.6 * hold_ + 0.4 * (a + b);
            }
        }

        const double mix = (a + b) * (0.7 + macroMod * 0.5) + hold_ * 0.15;
        return std::tanh(mix * 2.0) * 14500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    static bool euclidGate(int length, int pulses, int step, int rotate) {
        const int s = (step + rotate) % length;
        return ((s * pulses) % length) < pulses;
    }

    static double edoFreq(int degree, int edo, int oct, double base) {
        const double steps = static_cast<double>(degree) / static_cast<double>(edo);
        return base * std::pow(2.0, steps + static_cast<double>(oct));
    }

    void advanceA(std::uint64_t sampleIndex,
                  const std::vector<std::uint8_t>& memory,
                  double macroMod) {
        if (stepA_ == 0) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 1024ULL), memorySize_);
            pulsesA_ = 3 + (memory[idx] % 8);
            rotateA_ = memory[boundedIndex(idx + 1, memorySize_)] % 13;
        }

        if (euclidGate(13, pulsesA_, stepA_, rotateA_)) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(stepA_ * 31 + sampleIndex % 4096ULL), memorySize_);
            const int degree = memory[idx] % 31;
            const int oct = static_cast<int>(memory[boundedIndex(idx + 1, memorySize_)] % 3) - 1;
            const double freq = edoFreq(degree, 31, oct, 80.0 + macroMod * 40.0);
            freqA_ = std::clamp(freq, 24.0, 5200.0);
            envA_ = 1.0;
        }

        ++stepA_;
        if (stepA_ >= 13) {
            stepA_ = 0;
        }

        const double unit = static_cast<double>(sampleRate_) * 60.0 / (std::max(55.0, bpm_) * 20.0);
        samplesToA_ = std::max(1, static_cast<int>(unit * (1.0 + macroMod * 0.5 + p1_ * 0.02)));
    }

    void advanceB(std::uint64_t sampleIndex,
                  const std::vector<std::uint8_t>& memory,
                  double macroMod) {
        if (stepB_ == 0) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>((sampleIndex + 7ULL) % 2048ULL), memorySize_);
            pulsesB_ = 4 + (memory[idx] % 11);
            rotateB_ = memory[boundedIndex(idx + 1, memorySize_)] % 17;
        }

        if (euclidGate(17, pulsesB_, stepB_, rotateB_)) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(stepB_ * 47 + sampleIndex % 8192ULL), memorySize_);
            const int degree = memory[idx] % 19;
            const int oct = static_cast<int>(memory[boundedIndex(idx + 1, memorySize_)] % 3) - 1;
            const double freq = edoFreq(degree, 19, oct, 120.0 + (1.0 - macroMod) * 60.0);
            freqB_ = std::clamp(freq, 24.0, 5200.0);
            envB_ = 1.0;
        }

        ++stepB_;
        if (stepB_ >= 17) {
            stepB_ = 0;
        }

        const double unit = static_cast<double>(sampleRate_) * 60.0 / (std::max(55.0, bpm_) * 28.0);
        samplesToB_ = std::max(1, static_cast<int>(unit * (1.0 + (1.0 - macroMod) * 0.5)));

        ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(1 + pulsesA_ + pulsesB_), memorySize_);
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double bpm_;
    double p1_;

    double phaseA_;
    double phaseB_;
    double envA_;
    double envB_;
    double freqA_;
    double freqB_;

    int stepA_;
    int stepB_;
    int pulsesA_;
    int pulsesB_;
    int rotateA_;
    int rotateB_;
    int samplesToA_;
    int samplesToB_;
    double hold_;
};

class TritaveOddMeterChords final : public IRamAlgorithm {
public:
    TritaveOddMeterChords(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          bpm_(randomDouble(rng, 78.0, 150.0)),
          p1_(randomDouble(rng, 0.1, 10.0)),
          phase_{0.0, 0.0, 0.0},
          freq_{110.0, 140.0, 180.0},
          env_{0.0, 0.0, 0.0},
          samplesToHit_(1),
          patternPos_(0),
          lowpass_(0.0),
          patternUnits_{5, 7, 3, 6, 4, 9} {}

    const std::string& id() const override {
        static const std::string kId = "tritave_odd_meter_chords";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        if (samplesToHit_ <= 0) {
            trigger(sampleIndex, memory, macroMod);
        }
        --samplesToHit_;

        double sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            env_[i] *= (0.9991 - macroMod * 0.00025 + i * 0.00004);
            if (env_[i] < 0.00005) {
                env_[i] = 0.00005;
            }

            phase_[i] += (2.0 * kPi * freq_[i]) / static_cast<double>(sampleRate_);
            if (phase_[i] >= 2.0 * kPi) {
                phase_[i] -= 2.0 * kPi;
            }

            const double tone = std::sin(phase_[i]) + 0.22 * std::sin(phase_[i] * (2.0 + i * 0.4));
            sum += tone * env_[i] * (0.8 - i * 0.15);
        }

        const double cutoff = 0.018 + macroMod * 0.12;
        lowpass_ += cutoff * (sum - lowpass_);

        if ((sampleIndex % 97ULL) == 0ULL) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 4096ULL), memorySize_);
            if ((memory[idx] & 0x03U) == 0U) {
                lowpass_ *= 0.45;
            }
        }

        return std::tanh(lowpass_ * 2.2) * 15000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    void trigger(std::uint64_t sampleIndex,
                 const std::vector<std::uint8_t>& memory,
                 double macroMod) {
        static const std::array<std::array<int, 3>, 4> kChords = {
            std::array<int, 3>{0, 4, 7},
            std::array<int, 3>{0, 5, 9},
            std::array<int, 3>{0, 3, 8},
            std::array<int, 3>{0, 6, 10},
        };

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 4096ULL), memorySize_);
        const std::uint8_t b0 = memory[idx];
        const std::uint8_t b1 = memory[boundedIndex(idx + 1, memorySize_)];
        const std::uint8_t b2 = memory[boundedIndex(idx + 2, memorySize_)];

        const int root = b0 % 13;
        const int chordIdx = b1 % static_cast<int>(kChords.size());
        const int reg = static_cast<int>(b2 % 4) - 2;
        const double base = 72.0 * std::pow(2.0, static_cast<double>(reg));

        for (int i = 0; i < 3; ++i) {
            const int degree = root + kChords[chordIdx][i];
            const double ratio = std::pow(3.0, static_cast<double>(degree) / 13.0);
            freq_[i] = std::clamp(base * ratio, 24.0, 6200.0);
            env_[i] = 1.0 - i * 0.08;
        }

        int units = patternUnits_[patternPos_ % patternUnits_.size()] + static_cast<int>(b0 & 0x03U) - 1;
        if (units < 1) {
            units = 1;
        }

        const double unitSamples = static_cast<double>(sampleRate_) * 60.0 / (std::max(55.0, bpm_) * 24.0);
        samplesToHit_ = std::max(1, static_cast<int>(unitSamples * static_cast<double>(units)));

        bpm_ += (macroMod - 0.5) * 0.05 + (static_cast<int>(b1 % 5) - 2) * 0.02 * p1_;
        bpm_ = std::clamp(bpm_, 58.0, 205.0);

        ++patternPos_;
        ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(b0 + b1 + b2 + 5U), memorySize_);
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double bpm_;
    double p1_;
    std::array<double, 3> phase_;
    std::array<double, 3> freq_;
    std::array<double, 3> env_;
    int samplesToHit_;
    std::size_t patternPos_;
    double lowpass_;
    std::array<int, 6> patternUnits_;
};

}  // namespace

namespace ram_audio {

void registerMusicalAlgorithms(AlgorithmRegistry& registry) {
    registry.registerAlgorithm({
        "microtonal_glitch_grid",
        "Microtonal Glitch Grid",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<MicrotonalGlitchGrid>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "polymeter_euclidean_micro",
        "Polymeter Euclidean Micro",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<PolymeterEuclideanMicro>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "tritave_odd_meter_chords",
        "Tritave Odd Meter Chords",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<TritaveOddMeterChords>(memorySize, sampleRate, rng);
        },
    });
}

}  // namespace ram_audio
