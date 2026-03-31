#include "algorithm_registry.hpp"
#include "common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace ram_audio::algo_detail;

class RamWaveletScanner final : public IRamAlgorithm {
public:
    RamWaveletScanner(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(128, memorySize)),
          sampleRate_(std::max(1, sampleRate)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          phaseLow_(0.0),
          phaseMid_(0.0),
          phaseHigh_(0.0),
          hold_(0.0) {}

    const std::string& id() const override {
        static const std::string kId = "ram_wavelet_scanner";
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

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>((sampleIndex * 17ULL) % 65536ULL), memorySize_);
        const std::size_t i1 = boundedIndex(idx + 1, memorySize_);
        const std::size_t i2 = boundedIndex(idx + 2, memorySize_);
        const std::size_t i3 = boundedIndex(idx + 3, memorySize_);

        const double s0 = (static_cast<double>(memory[idx]) - 128.0) / 128.0;
        const double s1 = (static_cast<double>(memory[i1]) - 128.0) / 128.0;
        const double s2 = (static_cast<double>(memory[i2]) - 128.0) / 128.0;
        const double s3 = (static_cast<double>(memory[i3]) - 128.0) / 128.0;

        const double low = (s0 + s1 + s2 + s3) * 0.25;
        const double mid = ((s0 + s1) - (s2 + s3)) * 0.5;
        const double high = (s0 - s1 - s2 + s3) * 0.5;

        const double fLow = 40.0 + (0.5 + low * 0.5) * (140.0 + macroMod * 200.0);
        const double fMid = 140.0 + (0.5 + std::fabs(mid) * 0.5) * (900.0 + macroMod * 1200.0);
        const double fHigh = 700.0 + (0.5 + std::fabs(high) * 0.5) * (3200.0 + macroMod * 2200.0);

        phaseLow_ += (2.0 * kPi * fLow) / static_cast<double>(sampleRate_);
        phaseMid_ += (2.0 * kPi * fMid) / static_cast<double>(sampleRate_);
        phaseHigh_ += (2.0 * kPi * fHigh) / static_cast<double>(sampleRate_);
        wrapPhase(phaseLow_);
        wrapPhase(phaseMid_);
        wrapPhase(phaseHigh_);

        const double vLow = std::sin(phaseLow_) * (0.5 + 0.5 * std::fabs(low));
        const double vMid = std::sin(phaseMid_ + low * 1.2) * (0.3 + 0.7 * std::fabs(mid));
        const double vHigh = std::sin(phaseHigh_ + mid * 1.7) * (0.2 + 0.8 * std::fabs(high));

        hold_ = 0.985 * hold_ + 0.015 * (vLow * 0.55 + vMid * 0.30 + vHigh * 0.15);

        if ((sampleIndex & 0x0FFFULL) == 0ULL) {
            const std::size_t step = static_cast<std::size_t>(1 + memory[idx]);
            ptr_ = boundedIndex(ptr_ + step + static_cast<std::size_t>(p1_ * 17.0), memorySize_);
        }

        return std::tanh(hold_ * 3.2) * 15000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(128, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    static void wrapPhase(double& p) {
        while (p >= 2.0 * kPi) {
            p -= 2.0 * kPi;
        }
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    double phaseLow_;
    double phaseMid_;
    double phaseHigh_;
    double hold_;
};

class MarkovByteLattice final : public IRamAlgorithm {
public:
    MarkovByteLattice(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(std::max(1, sampleRate)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          stepSamples_(std::max(1, sampleRate_ / 14)),
          stepCounter_(0),
          state_(0),
          phase_(0.0),
          env_(0.0),
          rng_(&rng) {
        transition_.fill(1.0 / 16.0);
    }

    const std::string& id() const override {
        static const std::string kId = "markov_byte_lattice";
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

        ++stepCounter_;
        if (stepCounter_ >= stepSamples_) {
            stepCounter_ = 0;
            updateTransition(memory, sampleIndex, macroMod);
            state_ = pickNextState();
            env_ = 1.0;
            stepSamples_ = std::max(1, static_cast<int>(
                static_cast<double>(sampleRate_) / (6.0 + macroMod * 20.0 + p1_ * 0.2)));
        }

        env_ *= 0.9988;
        if (env_ < 0.0001) {
            env_ = 0.0001;
        }

        static const std::array<int, 16> semis = {
            0, 2, 3, 5, 7, 8, 10, 12,
            14, 15, 17, 19, 20, 22, 24, 27,
        };
        const double freq = 55.0 * std::pow(2.0, static_cast<double>(semis[state_]) / 12.0);
        phase_ += (2.0 * kPi * freq) / static_cast<double>(sampleRate_);
        while (phase_ >= 2.0 * kPi) {
            phase_ -= 2.0 * kPi;
        }

        const double tri = 2.0 * std::asin(std::sin(phase_)) / kPi;
        const double sq = std::sin(phase_ * (1.0 + macroMod * 0.4)) >= 0.0 ? 1.0 : -1.0;
        const double out = tri * (0.72 - macroMod * 0.2) + sq * (0.18 + macroMod * 0.22);

        if (state_ == prevState_) {
            ++sameStateCount_;
        } else {
            sameStateCount_ = 0;
            prevState_ = state_;
        }

        const int stateHoldLimit = 14;
        if (sameStateCount_ > stateHoldLimit && rng_ != nullptr) {
            std::uniform_int_distribution<int> jump(0, 15);
            int forced = jump(*rng_);
            if (forced == state_) {
                forced = (forced + 5) % 16;
            }
            state_ = forced;
            sameStateCount_ = 0;
            env_ = std::max(env_, 0.8);
        }

        return std::tanh(out * 2.3) * env_ * 14500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    void updateTransition(const std::vector<std::uint8_t>& memory,
                          std::uint64_t sampleIndex,
                          double macroMod) {
        std::array<double, 16> counts{};
        counts.fill(1.0);

        const std::size_t base = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 8192ULL), memorySize_);
        for (int i = 0; i < 64; ++i) {
            const std::size_t idx = boundedIndex(base + static_cast<std::size_t>(i * 37), memorySize_);
            const int to = static_cast<int>(memory[idx] >> 4U);
            counts[to] += 1.0 + macroMod * 0.5;
        }

        double sum = 0.0;
        for (double c : counts) {
            sum += c;
        }
        if (sum <= 1e-9) {
            sum = 1.0;
        }

        for (int i = 0; i < 16; ++i) {
            transition_[i] = counts[i] / sum;
        }

        ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(17 + memory[base]), memorySize_);
    }

    int pickNextState() {
        std::discrete_distribution<int> dist(transition_.begin(), transition_.end());
        if (rng_ == nullptr) {
            return 0;
        }
        return dist(*rng_);
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    int stepSamples_;
    int stepCounter_;
    int state_;
    int prevState_ = -1;
    int sameStateCount_ = 0;
    double phase_;
    double env_;
    std::array<double, 16> transition_;
    std::mt19937* rng_;
};

}  // namespace

namespace ram_audio {

void registerExperimentalAlgorithms(AlgorithmRegistry& registry) {
    registry.registerAlgorithm({
        "ram_wavelet_scanner",
        "RAM Wavelet Scanner",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<RamWaveletScanner>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "markov_byte_lattice",
        "Markov Byte Lattice",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<MarkovByteLattice>(memorySize, sampleRate, rng);
        },
    });
}

}  // namespace ram_audio
