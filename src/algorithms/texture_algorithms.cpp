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

}  // namespace

namespace ram_audio {

void registerTextureAlgorithms(AlgorithmRegistry& registry) {
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
        "fractal_byte_terrain",
        "Fractal Byte Terrain",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<FractalByteTerrain>(memorySize, sampleRate, rng);
        },
    });
}

}  // namespace ram_audio
