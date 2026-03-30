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

class ChaoticLorenzFm final : public IRamAlgorithm {
public:
    ChaoticLorenzFm(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          baseFreq_(randomDouble(rng, 55.0, 660.0)),
          p1_(randomDouble(rng, 0.1, 10.0)),
          x_(randomDouble(rng, -3.0, 3.0)),
          y_(randomDouble(rng, -3.0, 3.0)),
          z_(randomDouble(rng, 10.0, 35.0)),
          phase_(0.0) {}

    const std::string& id() const override {
        static const std::string kId = "chaotic_lorenz_fm";
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

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex * 3ULL), memorySize_);
        const double m = (static_cast<double>(memory[idx]) - 128.0) / 128.0;

        const double dt = 0.003 + 0.004 * macroMod;
        const double sigma = 10.0 + 2.0 * m;
        const double rho = 28.0 + 8.0 * macroMod + 2.0 * m;
        const double beta = 8.0 / 3.0;

        const double dx = sigma * (y_ - x_);
        const double dy = x_ * (rho - z_) - y_;
        const double dz = x_ * y_ - beta * z_;

        x_ += dt * dx;
        y_ += dt * dy;
        z_ += dt * dz;

        const double chaos = std::tanh((x_ + y_) * 0.08);
        const double fmIndex = 2.0 + 24.0 * (macroMod * 0.7 + std::abs(m) * 0.3);
        const double freq = std::max(20.0, baseFreq_ * (0.5 + p1_ * 0.12));
        phase_ += (2.0 * kPi * freq) / static_cast<double>(sampleRate_);
        if (phase_ > 2.0 * kPi) {
            phase_ -= 2.0 * kPi;
        }

        return std::sin(phase_ + chaos * fmIndex) * 12000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double baseFreq_;
    double p1_;
    double x_;
    double y_;
    double z_;
    double phase_;
};

class ResonatorBankEntropy final : public IRamAlgorithm {
public:
    ResonatorBankEntropy(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          a0_(0.0),
          a1_(0.0),
          a2_(0.0),
          b1_(0.0),
          b2_(0.0),
          z1_(0.0),
          z2_(0.0),
          updateInterval_(std::max(32, sampleRate / 15)),
          updateCounter_(0) {
        updateCoeffs(0.5, 0.0);
    }

    const std::string& id() const override {
        static const std::string kId = "resonator_bank_entropy";
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

        ++updateCounter_;
        if (updateCounter_ >= updateInterval_) {
            updateCounter_ = 0;

            std::uint32_t accum = 0;
            std::uint32_t accum2 = 0;
            for (int i = 0; i < 64; ++i) {
                const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(i * 97), memorySize_);
                const std::uint8_t v = memory[idx];
                accum += v;
                accum2 += static_cast<std::uint32_t>(v) * static_cast<std::uint32_t>(v);
            }

            const double mean = static_cast<double>(accum) / 64.0;
            const double var = std::max(1.0, (static_cast<double>(accum2) / 64.0) - (mean * mean));
            const double normVar = std::min(1.0, var / (128.0 * 128.0));

            const double chaos = static_cast<double>(memory[boundedIndex(ptr_ + sampleIndex % 4096ULL, memorySize_)]) / 255.0;
            updateCoeffs(normVar, macroMod * 0.6 + chaos * 0.4);

            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(mean + p1_ * 33.0), memorySize_);
            updateInterval_ = std::max(16, static_cast<int>(sampleRate_ / (8.0 + macroMod * 30.0)));
        }

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 16384ULL), memorySize_);
        const double in = (static_cast<double>(memory[idx]) - 128.0) / 128.0;

        const double out = (a0_ * in) + (a1_ * z1_) + (a2_ * z2_) - (b1_ * z1_) - (b2_ * z2_);
        z2_ = z1_;
        z1_ = out;

        return std::tanh(out * 2.8) * 13000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    void updateCoeffs(double entropy, double motion) {
        const double nyquist = static_cast<double>(sampleRate_) * 0.5;
        const double center = 80.0 + entropy * (nyquist * 0.8 - 80.0);
        const double q = 0.4 + (1.0 - std::min(1.0, motion)) * 14.0;

        const double omega = 2.0 * kPi * center / static_cast<double>(sampleRate_);
        const double sinw = std::sin(omega);
        const double cosw = std::cos(omega);
        const double alpha = sinw / (2.0 * q);

        double b0 = alpha;
        double b1 = 0.0;
        double b2 = -alpha;
        double aa0 = 1.0 + alpha;
        double aa1 = -2.0 * cosw;
        double aa2 = 1.0 - alpha;

        if (std::abs(aa0) < 1e-9) {
            aa0 = 1.0;
        }

        a0_ = b0 / aa0;
        a1_ = b1 / aa0;
        a2_ = b2 / aa0;
        b1_ = aa1 / aa0;
        b2_ = aa2 / aa0;
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;

    double a0_;
    double a1_;
    double a2_;
    double b1_;
    double b2_;
    double z1_;
    double z2_;

    int updateInterval_;
    int updateCounter_;
};

class RingModBitplanes final : public IRamAlgorithm {
public:
    RingModBitplanes(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptrA_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          ptrB_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          phase_(0.0) {}

    const std::string& id() const override {
        static const std::string kId = "ring_mod_bitplanes";
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

        const std::size_t idxA = boundedIndex(ptrA_ + static_cast<std::size_t>(sampleIndex * (1 + static_cast<std::uint64_t>(p1_))), memorySize_);
        const std::size_t idxB = boundedIndex(ptrB_ + static_cast<std::size_t>(sampleIndex * (3 + static_cast<std::uint64_t>(macroMod * 9.0))), memorySize_);

        const std::uint8_t a = memory[idxA];
        const std::uint8_t b = memory[idxB];

        const double hi = static_cast<double>((a & 0xF0U) ^ (b & 0xF0U)) / 240.0;
        const double lo = static_cast<double>((a & 0x0FU) ^ (b & 0x0FU)) / 15.0;
        const double carrierFreq = 40.0 + hi * 1200.0;

        phase_ += (2.0 * kPi * carrierFreq) / static_cast<double>(sampleRate_);
        if (phase_ > 2.0 * kPi) {
            phase_ -= 2.0 * kPi;
        }

        const double carrier = std::sin(phase_);
        const double mod = (lo * 2.0 - 1.0);
        const double out = carrier * mod;

        if ((sampleIndex & 0x7FFULL) == 0ULL) {
            ptrA_ = boundedIndex(ptrA_ + static_cast<std::size_t>(a + 1U), memorySize_);
            ptrB_ = boundedIndex(ptrB_ + static_cast<std::size_t>(b + 7U), memorySize_);
        }

        return std::tanh(out * 4.0) * 14500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptrA_ %= memorySize_;
        ptrB_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptrA_;
    std::size_t ptrB_;
    double p1_;
    double phase_;
};

class KarplusRamString final : public IRamAlgorithm {
public:
    KarplusRamString(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          delay_{},
          delaySize_(256),
          writePos_(0),
          feedback_(0.986),
          triggerInterval_(std::max(1, sampleRate / randomIntInclusive(rng, 3, 12))),
          triggerCounter_(0) {
        fillDelay();
    }

    const std::string& id() const override {
        static const std::string kId = "karplus_ram_string";
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

        ++triggerCounter_;
        if (triggerCounter_ >= triggerInterval_) {
            triggerCounter_ = 0;
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 1024ULL), memorySize_);
            const double ctrl = static_cast<double>(memory[idx]) / 255.0;
            const int maxDelay = static_cast<int>(delay_.size()) - 1;
            delaySize_ = std::clamp(
                static_cast<int>(64 + ctrl * (sampleRate_ / 6.0)),
                32,
                std::max(32, maxDelay));
            feedback_ = 0.90 + (1.0 - ctrl) * 0.097;
            triggerInterval_ = std::max(1, static_cast<int>(sampleRate_ / (1.5 + macroMod * 6.0 + p1_ * 0.2)));
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(memory[idx] + 19U), memorySize_);
            fillDelay();
        }

        const int bufSize = static_cast<int>(delay_.size());
        const int effectiveDelay = std::clamp(delaySize_, 1, std::max(1, bufSize - 1));

        int readPos = writePos_ - effectiveDelay;
        if (readPos < 0) {
            readPos += bufSize;
        }
        const int nextPos = (readPos + 1) % bufSize;

        const double y = delay_[readPos];
        const double y2 = delay_[nextPos];
        const double v = 0.5 * (y + y2) * feedback_;

        delay_[writePos_] = v;
        writePos_ = (writePos_ + 1) % static_cast<int>(delay_.size());

        return std::tanh(v * 3.2) * 15500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    void fillDelay() {
        for (std::size_t i = 0; i < delay_.size(); ++i) {
            const std::size_t idx = boundedIndex(ptr_ + i * 7U, memorySize_);
            const double n = (static_cast<double>(idx % 255U) - 128.0) / 128.0;
            delay_[i] = n;
        }
        writePos_ = 0;
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    std::array<double, 4096> delay_;
    int delaySize_;
    int writePos_;
    double feedback_;
    int triggerInterval_;
    int triggerCounter_;
};

}  // namespace

namespace ram_audio {

void registerAdvancedAlgorithms(AlgorithmRegistry& registry) {
    registry.registerAlgorithm({
        "chaotic_lorenz_fm",
        "Chaotic Lorenz FM",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<ChaoticLorenzFm>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "resonator_bank_entropy",
        "Resonator Bank Entropy",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<ResonatorBankEntropy>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "ring_mod_bitplanes",
        "Ring Mod Bitplanes",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<RingModBitplanes>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "karplus_ram_string",
        "Karplus RAM String",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<KarplusRamString>(memorySize, sampleRate, rng);
        },
    });
}

}  // namespace ram_audio
