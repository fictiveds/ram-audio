#include "algorithm_registry.hpp"
#include "common.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace ram_audio::algo_detail;

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

}  // namespace

namespace ram_audio {

void registerClassicAlgorithms(AlgorithmRegistry& registry) {
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
}

}  // namespace ram_audio
