#include "algorithms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

constexpr double kPi = 3.14159265358979323846;

int randomIntInclusive(std::mt19937& rng, int minVal, int maxVal) {
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(rng);
}

double randomDouble(std::mt19937& rng, double minVal, double maxVal) {
    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return dist(rng);
}

std::size_t boundedIndex(std::size_t idx, std::size_t memorySize) {
    if (memorySize == 0) {
        return 0;
    }
    return idx % memorySize;
}

std::int16_t readInt16LE(const std::vector<std::uint8_t>& memory, std::size_t idx) {
    if (memory.size() < 2) {
        return 0;
    }
    const std::size_t i0 = idx % (memory.size() - 1);
    const std::uint16_t lo = memory[i0];
    const std::uint16_t hi = memory[i0 + 1];
    return static_cast<std::int16_t>((hi << 8U) | lo);
}

class HilbertDrone final : public IRamAlgorithm {
public:
    HilbertDrone(std::size_t memorySize, int /*sampleRate*/, std::mt19937& rng)
        : memorySize_(memorySize), p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "hilbert_drone";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }
        const std::uint64_t rs = static_cast<std::uint64_t>(1 + macroMod * 15.0 * p1_);
        const std::uint64_t x = (sampleIndex * rs) ^ ((sampleIndex * rs) >> 1U);
        const std::size_t idx = boundedIndex(
            static_cast<std::size_t>((x * 2654435761ULL) % static_cast<std::uint64_t>(memorySize_)),
            memorySize_);
        return (static_cast<int>(memory[idx]) - 128) * 350.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
    }

private:
    std::size_t memorySize_;
    double p1_;
};

class BitSlicingArpeggios final : public IRamAlgorithm {
public:
    BitSlicingArpeggios(std::size_t memorySize, int /*sampleRate*/, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "bit_slicing_arpeggios";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }
        const std::size_t idx = boundedIndex(
            ptr_ + static_cast<std::size_t>(static_cast<double>(sampleIndex) * p1_),
            memorySize_);
        const std::uint8_t mask = macroMod > 0.5 ? 0xF0 : 0x0F;
        const std::uint8_t a = static_cast<std::uint8_t>(memory[idx] & mask);
        const std::uint8_t b = static_cast<std::uint8_t>(memory[(idx + memorySize_ - 1024 % memorySize_) % memorySize_] & mask);
        const int val = static_cast<int>(a ^ b);
        return (val - 128) * 500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    std::size_t ptr_;
    double p1_;
};

class WavefoldingDeltaBass final : public IRamAlgorithm {
public:
    WavefoldingDeltaBass(std::size_t memorySize, int /*sampleRate*/, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          accum_(0.0) {}

    const std::string& id() const override {
        static const std::string kId = "wavefolding_delta_bass";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }
        const std::size_t idx = boundedIndex(
            ptr_ + static_cast<std::size_t>(static_cast<double>(sampleIndex) * 0.2 * p1_),
            memorySize_);
        accum_ += (static_cast<int>(memory[idx]) - 128) * (1.0 + macroMod * 4.0);
        if (accum_ > 16000.0) {
            accum_ -= 32000.0;
        } else if (accum_ < -16000.0) {
            accum_ += 32000.0;
        }
        return accum_;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    std::size_t ptr_;
    double p1_;
    double accum_;
};

class WavetableGranularLoop final : public IRamAlgorithm {
public:
    WavetableGranularLoop(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          p2_(randomIntInclusive(rng, 8, 2048)),
          p3_(randomIntInclusive(rng, 1, 255)) {}

    const std::string& id() const override {
        static const std::string kId = "wavetable_granular_loop";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }
        const int frag = static_cast<int>(p2_ + macroMod * 100.0);
        const std::size_t idx = boundedIndex(ptr_ + (sampleIndex % static_cast<std::uint64_t>(std::max(1, frag))), memorySize_);
        const std::uint8_t val = memory[idx];

        const int period = std::max(1, static_cast<int>(sampleRate_ * p1_));
        if (sampleIndex % static_cast<std::uint64_t>(period) == 0ULL) {
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(val) * static_cast<std::size_t>(p3_), memorySize_);
        }
        return (static_cast<int>(val) - 128) * 400.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    int p2_;
    int p3_;
};

class LowPassFloatAliasing final : public IRamAlgorithm {
public:
    LowPassFloatAliasing(std::size_t memorySize, int /*sampleRate*/, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(4, memorySize)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "low_pass_float_aliasing";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }
        const std::size_t idx = boundedIndex(
            ptr_ + static_cast<std::size_t>(static_cast<double>(sampleIndex) * 0.5 * p1_),
            memorySize_ - 1);
        const int val8 = static_cast<int>(memory[idx]) - 128;
        const std::int16_t val16 = readInt16LE(memory, idx);
        return (val8 * 256.0 * (1.0 - macroMod)) + (static_cast<double>(val16) * macroMod * 0.5);
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(4, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    std::size_t ptr_;
    double p1_;
};

class MemoryPhaseModulation final : public IRamAlgorithm {
public:
    MemoryPhaseModulation(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "memory_phase_modulation";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double /*macroMod*/) override {
        if (memory.empty()) {
            return 0.0;
        }
        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(static_cast<double>(sampleIndex) * p1_), memorySize_);
        const double mod = static_cast<double>(memory[idx]) / 255.0;
        const double phase = (static_cast<double>(sampleIndex) * 440.0 * p1_ / static_cast<double>(sampleRate_)) + mod * 15.0;
        return std::sin(phase) * 10000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
};

class PercussiveRhythmTriggers final : public IRamAlgorithm {
public:
    PercussiveRhythmTriggers(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          timer_(0),
          p1_(randomDouble(rng, 0.1, 10.0)),
          p3_(randomIntInclusive(rng, 1, 255)) {}

    const std::string& id() const override {
        static const std::string kId = "percussive_rhythm_triggers";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double /*macroMod*/) override {
        if (memory.empty()) {
            return 0.0;
        }
        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex / 100ULL), memorySize_);
        const std::uint8_t val = memory[idx];
        if (val > 240U && timer_ <= 0) {
            timer_ = std::max(1, static_cast<int>(sampleRate_ * 0.1 * p1_));
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(val) * static_cast<std::size_t>(p3_), memorySize_);
        }
        if (timer_ > 0) {
            --timer_;
            const double env = static_cast<double>(timer_) / std::max(1.0, sampleRate_ * 0.1 * p1_);
            return std::sin(static_cast<double>(timer_) * env * p1_) * 12000.0 * env;
        }
        return 0.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    int timer_;
    double p1_;
    int p3_;
};

class BytebeatProcessor final : public IRamAlgorithm {
public:
    BytebeatProcessor(std::size_t memorySize, int /*sampleRate*/, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "bytebeat_processor";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& /*memory*/,
                    double /*macroMod*/) override {
        const std::uint64_t t = static_cast<std::uint64_t>(static_cast<double>(sampleIndex) * p1_ * 0.1) + ptr_;
        const std::uint64_t val = (t * (((t >> 12U) | (t >> 8U)) & 63ULL & (t >> 4U))) & 255ULL;
        return (static_cast<int>(val) - 128) * 200.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    std::size_t ptr_;
    double p1_;
};

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

class PointerWalkMelody final : public IRamAlgorithm {
public:
    PointerWalkMelody(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          stepSamples_(std::max(1, sampleRate / randomIntInclusive(rng, 4, 14))),
          phase_(0.0),
          currentFreq_(110.0),
          lastTriggerSample_(0),
          env_(0.0),
          p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "pointer_walk_melody";
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

        if (sampleIndex == 0ULL || (sampleIndex - lastTriggerSample_) >= static_cast<std::uint64_t>(stepSamples_)) {
            lastTriggerSample_ = sampleIndex;

            const std::size_t i0 = boundedIndex(ptr_, memorySize_);
            const std::size_t i1 = boundedIndex(ptr_ + 1, memorySize_);
            const int raw = static_cast<int>(memory[i0] ^ memory[i1]);
            const int jump = (raw % 257) - 128;
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(jump + static_cast<int>(macroMod * 64.0) + 129), memorySize_);

            static const int pentatonic[] = {0, 3, 5, 7, 10};
            const int note = pentatonic[raw % 5] + ((raw / 5) % 3) * 12;
            currentFreq_ = 55.0 * std::pow(2.0, static_cast<double>(note) / 12.0);
            env_ = 1.0;
            stepSamples_ = std::max(1, static_cast<int>(sampleRate_ / (3.0 + macroMod * 11.0 + (p1_ * 0.2))));
        }

        const double phaseInc = (2.0 * kPi * currentFreq_) / static_cast<double>(sampleRate_);
        phase_ += phaseInc;
        if (phase_ > 2.0 * kPi) {
            phase_ -= 2.0 * kPi;
        }

        env_ *= 0.9995 - macroMod * 0.00025;
        if (env_ < 0.0001) {
            env_ = 0.0001;
        }

        const double tone = (std::sin(phase_) + 0.35 * std::sin(phase_ * 2.01)) * env_;
        return tone * 11000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    int stepSamples_;
    double phase_;
    double currentFreq_;
    std::uint64_t lastTriggerSample_;
    double env_;
    double p1_;
};

class GranularFreezeScrub final : public IRamAlgorithm {
public:
    GranularFreezeScrub(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          center_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          grainLen_(randomIntInclusive(rng, 128, 4096)),
          grainPos_(0),
          reverse_(false),
          freezeSamples_(randomIntInclusive(rng, sampleRate / 8, sampleRate * 2)),
          sinceReset_(0),
          p1_(randomDouble(rng, 0.1, 10.0)) {}

    const std::string& id() const override {
        static const std::string kId = "granular_freeze_scrub";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t /*sampleIndex*/,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        ++sinceReset_;
        if (sinceReset_ >= freezeSamples_) {
            const std::size_t wander = static_cast<std::size_t>((memory[(center_ + grainPos_) % memorySize_] + static_cast<int>(macroMod * 255.0)) % 2048);
            center_ = boundedIndex(center_ + wander + static_cast<std::size_t>(p1_ * 13.0), memorySize_);
            grainLen_ = std::max(64, static_cast<int>(256 + macroMod * 7000.0));
            reverse_ = (memory[center_ % memorySize_] & 0x1U) != 0;
            freezeSamples_ = std::max(sampleRate_ / 12, static_cast<int>(sampleRate_ * (0.25 + macroMod * 1.8)));
            sinceReset_ = 0;
            grainPos_ = 0;
        }

        const int pos = reverse_ ? (grainLen_ - 1 - grainPos_) : grainPos_;
        const std::size_t idx = boundedIndex(center_ + static_cast<std::size_t>(std::max(0, pos)), memorySize_);
        const std::size_t idx2 = boundedIndex(idx + static_cast<std::size_t>(16 + static_cast<int>(macroMod * 512.0)), memorySize_);

        const double a = (static_cast<double>(memory[idx]) - 128.0) / 128.0;
        const double b = (static_cast<double>(memory[idx2]) - 128.0) / 128.0;

        const double phase = static_cast<double>(grainPos_) / static_cast<double>(std::max(1, grainLen_));
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * phase);
        const double mixed = (a * 0.65 + b * 0.35) * window;

        grainPos_ = (grainPos_ + 1) % std::max(1, grainLen_);
        return mixed * 15000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        center_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t center_;
    int grainLen_;
    int grainPos_;
    bool reverse_;
    int freezeSamples_;
    int sinceReset_;
    double p1_;
};

class CellularAutomataNoise final : public IRamAlgorithm {
public:
    CellularAutomataNoise(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          rule_(static_cast<std::uint8_t>(randomIntInclusive(rng, 0, 255))),
          stepInterval_(std::max(1, sampleRate / randomIntInclusive(rng, 200, 2000))),
          stepCounter_(0),
          ca_{},
          tmp_{} {
        seedFromMemory();
    }

    const std::string& id() const override {
        static const std::string kId = "cellular_automata_noise";
        return kId;
    }

    bool prefersHighResolution() const override {
        return true;
    }

    double generate(std::uint64_t /*sampleIndex*/,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        ++stepCounter_;
        if (stepCounter_ >= stepInterval_) {
            stepCounter_ = 0;
            const std::size_t idx = boundedIndex(ptr_, memorySize_);
            rule_ = static_cast<std::uint8_t>((rule_ ^ memory[idx]) + static_cast<std::uint8_t>(macroMod * 31.0));
            evolve();

            if ((memory[idx] & 0x1FU) == 0U) {
                ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(memory[idx] + 33U), memorySize_);
                seedFromMemory();
            }
            stepInterval_ = std::max(1, static_cast<int>(sampleRate_ / (220.0 + macroMod * 2600.0)));
        }

        int acc = 0;
        for (std::size_t i = 0; i < ca_.size(); ++i) {
            if (ca_[i] != 0U) {
                acc += (i % 2 == 0) ? 1 : -1;
            }
        }
        const double v = static_cast<double>(acc) / static_cast<double>(ca_.size());
        return std::tanh(v * (5.0 + macroMod * 7.0)) * 14000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    void seedFromMemory() {
        for (std::size_t i = 0; i < ca_.size(); ++i) {
            const std::size_t idx = boundedIndex(ptr_ + i * 17, memorySize_);
            ca_[i] = static_cast<std::uint8_t>((idx & 1U) != 0U);
        }
        ca_[ca_.size() / 2] = 1U;
    }

    void evolve() {
        const std::size_t n = ca_.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t left = ca_[(i + n - 1) % n];
            const std::uint8_t center = ca_[i];
            const std::uint8_t right = ca_[(i + 1) % n];
            const std::uint8_t pattern = static_cast<std::uint8_t>((left << 2U) | (center << 1U) | right);
            tmp_[i] = static_cast<std::uint8_t>((rule_ >> pattern) & 1U);
        }
        ca_ = tmp_;
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    std::uint8_t rule_;
    int stepInterval_;
    int stepCounter_;
    std::array<std::uint8_t, 64> ca_;
    std::array<std::uint8_t, 64> tmp_;
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

class FractalByteTerrain final : public IRamAlgorithm {
public:
    FractalByteTerrain(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(sampleRate),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          x_(randomDouble(rng, -1.2, 1.2)),
          y_(randomDouble(rng, -1.2, 1.2)),
          dx_(randomDouble(rng, 0.0002, 0.002)),
          dy_(randomDouble(rng, 0.0002, 0.002)) {}

    const std::string& id() const override {
        static const std::string kId = "fractal_byte_terrain";
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

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 4096ULL), memorySize_);
        const double ram = static_cast<double>(memory[idx]) / 255.0;

        const double cRe = -0.78 + (ram - 0.5) * (0.7 + macroMod * 0.8);
        const double cIm = 0.156 + std::sin(p1_ * 0.2) * 0.2;

        double zx = x_;
        double zy = y_;
        int iter = 0;
        const int maxIter = 32;
        while (iter < maxIter) {
            const double zx2 = zx * zx - zy * zy + cRe;
            const double zy2 = 2.0 * zx * zy + cIm;
            zx = zx2;
            zy = zy2;
            if ((zx * zx + zy * zy) > 4.0) {
                break;
            }
            ++iter;
        }

        x_ += dx_ * (0.3 + macroMod * 2.0);
        y_ += dy_ * (0.3 + (1.0 - macroMod) * 2.0);
        if (x_ > 1.5 || x_ < -1.5) {
            dx_ = -dx_;
        }
        if (y_ > 1.5 || y_ < -1.5) {
            dy_ = -dy_;
        }

        if ((sampleIndex & 0x1FFFULL) == 0ULL) {
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(memory[idx] + 23U), memorySize_);
        }

        const double n = (static_cast<double>(iter) / static_cast<double>(maxIter)) * 2.0 - 1.0;
        return std::tanh(n * (3.0 + ram * 4.0)) * 13000.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    double x_;
    double y_;
    double dx_;
    double dy_;
};

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

bool AlgorithmRegistry::registerAlgorithm(AlgorithmEntry entry) {
    if (entry.id.empty() || !entry.factory) {
        return false;
    }
    if (indexById_.find(entry.id) != indexById_.end()) {
        return false;
    }
    indexById_[entry.id] = orderedEntries_.size();
    orderedEntries_.push_back(std::move(entry));
    return true;
}

bool AlgorithmRegistry::has(const std::string& id) const {
    return indexById_.find(id) != indexById_.end();
}

const AlgorithmEntry* AlgorithmRegistry::get(const std::string& id) const {
    const auto it = indexById_.find(id);
    if (it == indexById_.end()) {
        return nullptr;
    }
    return &orderedEntries_[it->second];
}

std::vector<AlgorithmEntry> AlgorithmRegistry::entries() const {
    return orderedEntries_;
}

std::vector<std::string> AlgorithmRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(orderedEntries_.size());
    for (const auto& e : orderedEntries_) {
        out.push_back(e.id);
    }
    return out;
}

std::unique_ptr<IRamAlgorithm> AlgorithmRegistry::create(const std::string& id,
                                                          std::size_t memorySize,
                                                          int sampleRate,
                                                          std::mt19937& rng) const {
    const AlgorithmEntry* entry = get(id);
    if (entry == nullptr) {
        return nullptr;
    }
    return entry->factory(memorySize, sampleRate, rng);
}

std::unique_ptr<IRamAlgorithm> AlgorithmRegistry::createRandom(const std::vector<std::string>& allowedIds,
                                                                std::size_t memorySize,
                                                                int sampleRate,
                                                                std::mt19937& rng) const {
    std::vector<const AlgorithmEntry*> candidates;
    candidates.reserve(orderedEntries_.size());

    if (allowedIds.empty()) {
        for (const auto& entry : orderedEntries_) {
            candidates.push_back(&entry);
        }
    } else {
        for (const auto& id : allowedIds) {
            const AlgorithmEntry* entry = get(id);
            if (entry != nullptr) {
                candidates.push_back(entry);
            }
        }
    }

    if (candidates.empty()) {
        return nullptr;
    }

    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    const AlgorithmEntry* selected = candidates[dist(rng)];
    return selected->factory(memorySize, sampleRate, rng);
}

AlgorithmRegistry createDefaultAlgorithmRegistry() {
    AlgorithmRegistry registry;

    registry.registerAlgorithm({
        "hilbert_drone",
        "Hilbert Drone",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<HilbertDrone>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "bit_slicing_arpeggios",
        "Bit-Slicing Arpeggios",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<BitSlicingArpeggios>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "wavefolding_delta_bass",
        "Wavefolding Delta Bass",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<WavefoldingDeltaBass>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "wavetable_granular_loop",
        "Wavetable Granular Loop",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<WavetableGranularLoop>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "low_pass_float_aliasing",
        "Low-pass Float Aliasing",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<LowPassFloatAliasing>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "memory_phase_modulation",
        "Memory Phase Modulation",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<MemoryPhaseModulation>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "percussive_rhythm_triggers",
        "Percussive Rhythm Triggers",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<PercussiveRhythmTriggers>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "bytebeat_processor",
        "Byte-beat Processor",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<BytebeatProcessor>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "chaotic_lorenz_fm",
        "Chaotic Lorenz FM",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<ChaoticLorenzFm>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "pointer_walk_melody",
        "Pointer Walk Melody",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<PointerWalkMelody>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "granular_freeze_scrub",
        "Granular Freeze Scrub",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<GranularFreezeScrub>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "cellular_automata_noise",
        "Cellular Automata Noise",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<CellularAutomataNoise>(memorySize, sampleRate, rng);
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

    registry.registerAlgorithm({
        "fractal_byte_terrain",
        "Fractal Byte Terrain",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<FractalByteTerrain>(memorySize, sampleRate, rng);
        },
    });

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

    return registry;
}
