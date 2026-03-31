#include "algorithm_registry.hpp"
#include "common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <random>
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
        return false;
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
        return false;
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

class FdnPrimeFeedback final : public IRamAlgorithm {
public:
    FdnPrimeFeedback(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(std::max(1, sampleRate)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          feedback_(randomDouble(rng, 0.78, 0.93)),
          wet_(randomDouble(rng, 0.45, 0.9)),
          exciteCounter_(0),
          modPhase_(0.0) {
        static const std::array<int, 6> kPrimeDelays = {149, 211, 283, 353, 431, 509};
        for (std::size_t i = 0; i < delay_.size(); ++i) {
            const int scale = std::max(1, sampleRate_ / 44100);
            const std::size_t len = static_cast<std::size_t>(kPrimeDelays[i] * scale);
            delay_[i].assign(std::max<std::size_t>(31, len), 0.0);
            pos_[i] = 0;
            lp_[i] = 0.0;
        }
    }

    const std::string& id() const override {
        static const std::string kId = "fdn_prime_feedback";
        return kId;
    }

    bool prefersHighResolution() const override {
        return false;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        static const int kSigns[6][6] = {
            {+1, +1, -1, +1, -1, +1},
            {+1, -1, +1, -1, +1, +1},
            {-1, +1, +1, +1, +1, -1},
            {+1, -1, +1, +1, -1, +1},
            {-1, +1, +1, -1, +1, +1},
            {+1, +1, -1, +1, +1, -1},
        };

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 65536ULL), memorySize_);
        const double memNorm = (static_cast<double>(memory[idx]) - 128.0) / 128.0;

        ++exciteCounter_;
        const bool pulse = (memory[idx] > 247U) || (memory[idx] < 8U) || (exciteCounter_ > sampleRate_ / 7);
        if (pulse) {
            exciteCounter_ = 0;
        }
        const double excite = memNorm * (pulse ? 1.35 : 0.22);

        std::array<double, 6> out{};
        for (std::size_t i = 0; i < delay_.size(); ++i) {
            out[i] = delay_[i][pos_[i]];
        }

        const double fb = feedback_ * (0.82 + macroMod * 0.16);
        for (std::size_t i = 0; i < delay_.size(); ++i) {
            double mix = 0.0;
            for (std::size_t j = 0; j < delay_.size(); ++j) {
                mix += static_cast<double>(kSigns[i][j]) * out[j];
            }
            mix /= 6.0;

            const double lineIn = excite * (i == 0 ? 1.0 : 0.22) + mix * fb;
            lp_[i] = lp_[i] * (0.93 - macroMod * 0.03) + lineIn * (0.07 + macroMod * 0.03);
            const double write = std::tanh((lineIn * 0.68 + lp_[i] * 0.32) * 2.0) * 0.985;

            delay_[i][pos_[i]] = write;
            pos_[i] = (pos_[i] + 1) % delay_[i].size();
        }

        modPhase_ += (2.0 * kPi * (0.08 + std::fabs(memNorm) * 0.25)) / static_cast<double>(sampleRate_);
        while (modPhase_ >= 2.0 * kPi) {
            modPhase_ -= 2.0 * kPi;
        }

        const double color = 0.82 + 0.18 * std::sin(modPhase_ + out[2] * 0.5);
        const double y = (out[0] * 0.42 - out[1] * 0.27 + out[2] * 0.19 - out[3] * 0.11 + out[4] * 0.08 - out[5] * 0.04) * color;

        if ((sampleIndex & 0x0FFFULL) == 0ULL) {
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(17U + memory[idx] + static_cast<int>(p1_ * 11.0)), memorySize_);
        }

        return std::tanh(y * (2.2 + macroMod * 0.8)) * 15000.0 * wet_;
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
    std::array<std::vector<double>, 6> delay_;
    std::array<std::size_t, 6> pos_;
    std::array<double, 6> lp_;
    double feedback_;
    double wet_;
    int exciteCounter_;
    double modPhase_;
};

class SpectralFreezePermuter final : public IRamAlgorithm {
public:
    SpectralFreezePermuter(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(std::max(1, sampleRate)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          freeze_(false),
          freezeBlocksLeft_(0),
          rng_(&rng) {
        initTables();
    }

    const std::string& id() const override {
        static const std::string kId = "spectral_freeze_permuter";
        return kId;
    }

    bool prefersHighResolution() const override {
        return false;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod) override {
        if (memory.empty()) {
            return 0.0;
        }

        if (outQueue_.empty()) {
            synthesizeBlock(memory, sampleIndex, macroMod);
        }

        if (outQueue_.empty()) {
            return 0.0;
        }

        const double out = outQueue_.front();
        outQueue_.pop_front();
        return out;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    static constexpr int kN = 128;
    static constexpr int kHop = 32;
    static constexpr int kBins = 24;

    void initTables() {
        for (int n = 0; n < kN; ++n) {
            window_[n] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(kN - 1));
        }

        for (int b = 0; b < kBins; ++b) {
            const int bin = b + 1;
            for (int n = 0; n < kN; ++n) {
                const double ph = 2.0 * kPi * static_cast<double>(bin * n) / static_cast<double>(kN);
                cosTable_[b * kN + n] = std::cos(ph);
                sinTable_[b * kN + n] = std::sin(ph);
            }
            perm_[b] = b;
        }
    }

    void synthesizeBlock(const std::vector<std::uint8_t>& memory,
                         std::uint64_t sampleIndex,
                         double macroMod) {
        const std::size_t stride = static_cast<std::size_t>(1 + static_cast<int>(macroMod * 3.0 + p1_ * 0.08));
        for (int n = 0; n < kN; ++n) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(n) * stride, memorySize_);
            const double x = (static_cast<double>(memory[idx]) - 128.0) / 128.0;
            frame_[n] = x * window_[n];
        }

        for (int b = 0; b < kBins; ++b) {
            double re = 0.0;
            double im = 0.0;
            const int base = b * kN;
            for (int n = 0; n < kN; ++n) {
                re += frame_[n] * cosTable_[base + n];
                im -= frame_[n] * sinTable_[base + n];
            }
            mags_[b] = std::sqrt(re * re + im * im) / static_cast<double>(kN);
            phases_[b] = std::atan2(im, re);
        }

        const std::size_t trigIdx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 4096ULL), memorySize_);
        const std::uint8_t trig = memory[trigIdx];

        if (!freeze_) {
            const double trigProb = 0.02 + macroMod * 0.08 + (trig > 230U ? 0.16 : 0.0);
            if (rng_ != nullptr && randomDouble(*rng_, 0.0, 1.0) < trigProb) {
                freeze_ = true;
                freezeBlocksLeft_ = 4 + (trig % 14);
                frozenMags_ = mags_;
                frozenPhases_ = phases_;
            }
        } else {
            --freezeBlocksLeft_;
            if (freezeBlocksLeft_ <= 0) {
                freeze_ = false;
            }
        }

        for (int i = 0; i < kBins; ++i) {
            perm_[i] = i;
        }

        const int depth = std::clamp(static_cast<int>((0.25 + macroMod * 0.70) * static_cast<double>(kBins)), 0, kBins - 1);
        if (rng_ != nullptr) {
            for (int i = 0; i < depth; ++i) {
                std::uniform_int_distribution<int> dist(i, kBins - 1);
                const int j = dist(*rng_);
                std::swap(perm_[i], perm_[j]);
            }
        }

        const auto& useMags = freeze_ ? frozenMags_ : mags_;
        const auto& usePhases = freeze_ ? frozenPhases_ : phases_;

        for (int n = 0; n < kN; ++n) {
            double s = 0.0;
            for (int b = 0; b < kBins; ++b) {
                const int src = perm_[b];
                const double ph = usePhases[src] + 2.0 * kPi * static_cast<double>((b + 1) * n) / static_cast<double>(kN);
                s += useMags[src] * std::cos(ph);
            }
            synth_[n] = s * window_[n] * 2.6;
        }

        for (int n = 0; n < kN; ++n) {
            overlap_[n] += synth_[n];
        }

        for (int n = 0; n < kHop; ++n) {
            outQueue_.push_back(std::tanh(overlap_[n] * (3.2 + macroMod * 0.6)) * 15500.0);
        }

        for (int n = 0; n < (kN - kHop); ++n) {
            overlap_[n] = overlap_[n + kHop];
        }
        for (int n = (kN - kHop); n < kN; ++n) {
            overlap_[n] = 0.0;
        }

        ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(17U + trig + static_cast<int>(p1_ * 9.0)), memorySize_);
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;

    std::array<double, kN> window_{};
    std::array<double, kN> frame_{};
    std::array<double, kN> synth_{};
    std::array<double, kN> overlap_{};
    std::array<double, kBins * kN> cosTable_{};
    std::array<double, kBins * kN> sinTable_{};

    std::array<double, kBins> mags_{};
    std::array<double, kBins> phases_{};
    std::array<double, kBins> frozenMags_{};
    std::array<double, kBins> frozenPhases_{};
    std::array<int, kBins> perm_{};

    std::deque<double> outQueue_;
    bool freeze_;
    int freezeBlocksLeft_;
    std::mt19937* rng_;
};

class ModalMeshExciter final : public IRamAlgorithm {
public:
    ModalMeshExciter(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(std::max(1, sampleRate)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          exciteInterval_(std::max(1, sampleRate_ / 16)),
          exciteCounter_(0),
          c2Base_(0.08),
          dampingBase_(0.002),
          outLP_(0.0),
          rng_(&rng) {
        prev_.fill(0.0);
        curr_.fill(0.0);
        next_.fill(0.0);
    }

    const std::string& id() const override {
        static const std::string kId = "modal_mesh_exciter";
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

        ++exciteCounter_;
        if (exciteCounter_ >= exciteInterval_) {
            exciteCounter_ = 0;
            excite(memory, sampleIndex, macroMod);
        }

        const double c2 = std::clamp(c2Base_ + macroMod * 0.12, 0.02, 0.24);
        const double damp = std::clamp(dampingBase_ + (1.0 - macroMod) * 0.008, 0.0004, 0.02);

        for (int y = 1; y < (kH - 1); ++y) {
            for (int x = 1; x < (kW - 1); ++x) {
                const int i = y * kW + x;
                const double lap = curr_[i - 1] + curr_[i + 1] + curr_[i - kW] + curr_[i + kW] - 4.0 * curr_[i];
                const double v = (2.0 - damp) * curr_[i] - (1.0 - damp) * prev_[i] + c2 * lap;
                next_[i] = std::clamp(v, -3.0, 3.0);
            }
        }

        for (int x = 0; x < kW; ++x) {
            next_[x] = 0.0;
            next_[(kH - 1) * kW + x] = 0.0;
        }
        for (int y = 0; y < kH; ++y) {
            next_[y * kW] = 0.0;
            next_[y * kW + (kW - 1)] = 0.0;
        }

        prev_.swap(curr_);
        curr_.swap(next_);
        next_.fill(0.0);

        const int c = (kH / 2) * kW + (kW / 2);
        const double o = curr_[c] + 0.6 * curr_[c - 1] + 0.45 * curr_[c + 1] + 0.4 * curr_[c - kW] + 0.35 * curr_[c + kW];
        outLP_ = outLP_ * 0.995 + o * 0.005;

        if ((sampleIndex & 0x07FFULL) == 0ULL) {
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(13U + static_cast<int>(p1_ * 11.0)), memorySize_);
        }

        return std::tanh((o + outLP_ * 0.25) * 2.2) * 15500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    static constexpr int kW = 12;
    static constexpr int kH = 12;
    static constexpr int kN = kW * kH;

    void excite(const std::vector<std::uint8_t>& memory,
                std::uint64_t sampleIndex,
                double macroMod) {
        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 4096ULL), memorySize_);
        const std::uint8_t b0 = memory[idx];
        const std::uint8_t b1 = memory[boundedIndex(idx + 1, memorySize_)];
        const std::uint8_t b2 = memory[boundedIndex(idx + 2, memorySize_)];

        const int x = 1 + (b0 % (kW - 2));
        const int y = 1 + (b1 % (kH - 2));
        const int i = y * kW + x;

        const double amp = ((static_cast<double>(b2) - 128.0) / 128.0) * (0.8 + macroMod * 1.8);
        curr_[i] += amp;
        curr_[i - 1] += amp * 0.3;
        curr_[i + 1] += amp * 0.3;
        curr_[i - kW] += amp * 0.2;
        curr_[i + kW] += amp * 0.2;

        if (rng_ != nullptr && randomDouble(*rng_, 0.0, 1.0) < (0.12 + macroMod * 0.22)) {
            std::uniform_int_distribution<int> xd(1, kW - 2);
            std::uniform_int_distribution<int> yd(1, kH - 2);
            const int rx = xd(*rng_);
            const int ry = yd(*rng_);
            curr_[ry * kW + rx] += amp * 0.55;
        }

        exciteInterval_ = std::max(1, static_cast<int>(
            static_cast<double>(sampleRate_) / (7.0 + macroMod * 22.0 + (static_cast<double>(b0 & 0x0FU) * 0.6))));
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    int exciteInterval_;
    int exciteCounter_;
    double c2Base_;
    double dampingBase_;
    double outLP_;
    std::array<double, kN> prev_{};
    std::array<double, kN> curr_{};
    std::array<double, kN> next_{};
    std::mt19937* rng_;
};

class BytebeatFormulaEvolver final : public IRamAlgorithm {
public:
    BytebeatFormulaEvolver(std::size_t memorySize, int sampleRate, std::mt19937& rng)
        : memorySize_(std::max<std::size_t>(1, memorySize)),
          sampleRate_(std::max(1, sampleRate)),
          ptr_(static_cast<std::size_t>(randomIntInclusive(rng, 0, static_cast<int>(memorySize_) - 1))),
          p1_(randomDouble(rng, 0.1, 10.0)),
          active_(0),
          evalInterval_(std::max(256, sampleRate_ / 3)),
          evalCounter_(0),
          noveltyAccum_(0.0),
          prevNorm_(0.0),
          zcAccum_(0),
          rng_(&rng),
          tOffset_(static_cast<std::uint32_t>(randomIntInclusive(rng, 0, 1 << 20))),
          formulaMix_(0.0) {
        for (auto& g : population_) {
            g = randomGenome();
        }
    }

    const std::string& id() const override {
        static const std::string kId = "bytebeat_formula_evolver";
        return kId;
    }

    double generate(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double /*macroMod*/) override {
        if (memory.empty()) {
            return 0.0;
        }

        const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(sampleIndex % 16384ULL), memorySize_);
        const std::uint8_t m = memory[idx];
        const std::uint32_t t = static_cast<std::uint32_t>((sampleIndex + static_cast<std::uint64_t>(tOffset_)) *
                                                           static_cast<std::uint64_t>(1 + static_cast<int>(p1_ * 2.0)));

        const std::uint8_t vA = evalGenome(population_[active_], t, m);
        const int alt = (active_ + 3) % static_cast<int>(population_.size());
        const std::uint8_t vB = evalGenome(population_[alt], t ^ (t >> 3U), static_cast<std::uint8_t>(m ^ 0x5AU));

        formulaMix_ = 0.996 * formulaMix_ + 0.004 * (static_cast<double>(m) / 255.0);
        const double blend = 0.25 + 0.55 * formulaMix_;
        const double normA = (static_cast<double>(vA) - 128.0) / 128.0;
        const double normB = (static_cast<double>(vB) - 128.0) / 128.0;
        const double norm = std::clamp(normA * (1.0 - blend) + normB * blend, -1.0, 1.0);

        noveltyAccum_ += std::fabs(norm - prevNorm_);
        if ((norm >= 0.0) != (prevNorm_ >= 0.0)) {
            ++zcAccum_;
        }
        prevNorm_ = norm;

        ++evalCounter_;
        if (evalCounter_ >= evalInterval_) {
            evolve(memory, sampleIndex);
            evalCounter_ = 0;
            noveltyAccum_ = 0.0;
            zcAccum_ = 0;
        }

        if ((sampleIndex & 0x07FFULL) == 0ULL) {
            ptr_ = boundedIndex(ptr_ + static_cast<std::size_t>(3U + m), memorySize_);
        }

        return std::tanh(norm * (3.8 + static_cast<double>(m) / 180.0)) * 16500.0;
    }

    void onMemorySizeChanged(std::size_t newMemorySize) override {
        memorySize_ = std::max<std::size_t>(1, newMemorySize);
        ptr_ %= memorySize_;
    }

private:
    struct Genome {
        std::uint8_t opA = 0;
        std::uint8_t opB = 0;
        std::uint8_t shiftA = 1;
        std::uint8_t shiftB = 1;
        std::uint8_t mix = 1;
        std::uint8_t mult = 1;
        std::uint8_t xorMask = 0;
        std::uint8_t add = 0;
    };

    Genome randomGenome() {
        Genome g;
        if (rng_ == nullptr) {
            return g;
        }

        std::uniform_int_distribution<int> opDist(0, 7);
        std::uniform_int_distribution<int> shiftDist(1, 15);
        std::uniform_int_distribution<int> byteDist(0, 255);
        std::uniform_int_distribution<int> mixDist(1, 6);

        g.opA = static_cast<std::uint8_t>(opDist(*rng_));
        g.opB = static_cast<std::uint8_t>(opDist(*rng_));
        g.shiftA = static_cast<std::uint8_t>(shiftDist(*rng_));
        g.shiftB = static_cast<std::uint8_t>(shiftDist(*rng_));
        g.mix = static_cast<std::uint8_t>(mixDist(*rng_));
        g.mult = static_cast<std::uint8_t>(1 + (byteDist(*rng_) % 23));
        g.xorMask = static_cast<std::uint8_t>(byteDist(*rng_));
        g.add = static_cast<std::uint8_t>(byteDist(*rng_));
        return g;
    }

    std::uint32_t evalOp(std::uint8_t op, std::uint32_t t, std::uint8_t m, std::uint8_t shift) const {
        const std::uint32_t s = std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(shift));
        switch (op & 7U) {
            case 0: return t;
            case 1: return t >> s;
            case 2: return t * static_cast<std::uint32_t>(m + 1U);
            case 3: return t & (t >> s);
            case 4: return t * (t >> s);
            case 5: return t | (t >> s);
            case 6: return t ^ (t >> s);
            default: return (t * static_cast<std::uint32_t>(m + 1U)) >> s;
        }
    }

    std::uint8_t evalGenome(const Genome& g, std::uint32_t t, std::uint8_t m) const {
        const std::uint32_t a = evalOp(g.opA, t, m, g.shiftA);
        const std::uint32_t b = evalOp(g.opB, t, m, g.shiftB);
        const std::uint32_t c = (a ^ (b << (g.mix & 7U))) + (static_cast<std::uint32_t>(m) * static_cast<std::uint32_t>(g.mult)) + g.add;
        return static_cast<std::uint8_t>((c ^ (c >> 8U) ^ g.xorMask) & 0xFFU);
    }

    std::uint64_t signature(const Genome& g) const {
        std::uint64_t s = 0;
        s |= static_cast<std::uint64_t>(g.opA);
        s |= static_cast<std::uint64_t>(g.opB) << 8U;
        s |= static_cast<std::uint64_t>(g.shiftA) << 16U;
        s |= static_cast<std::uint64_t>(g.shiftB) << 24U;
        s |= static_cast<std::uint64_t>(g.mix) << 32U;
        s |= static_cast<std::uint64_t>(g.mult) << 40U;
        s |= static_cast<std::uint64_t>(g.xorMask) << 48U;
        s |= static_cast<std::uint64_t>(g.add) << 56U;
        return s;
    }

    bool isTabu(std::uint64_t sig) const {
        return std::find(tabu_.begin(), tabu_.end(), sig) != tabu_.end();
    }

    Genome mutate(const Genome& base) {
        Genome g = base;
        if (rng_ == nullptr) {
            return g;
        }

        std::uniform_real_distribution<double> p(0.0, 1.0);
        std::uniform_int_distribution<int> opDist(0, 7);
        std::uniform_int_distribution<int> shiftDist(1, 15);
        std::uniform_int_distribution<int> byteDist(0, 255);

        if (p(*rng_) < 0.55) {
            g.opA = static_cast<std::uint8_t>(opDist(*rng_));
        }
        if (p(*rng_) < 0.55) {
            g.opB = static_cast<std::uint8_t>(opDist(*rng_));
        }
        if (p(*rng_) < 0.45) {
            g.shiftA = static_cast<std::uint8_t>(shiftDist(*rng_));
        }
        if (p(*rng_) < 0.45) {
            g.shiftB = static_cast<std::uint8_t>(shiftDist(*rng_));
        }
        if (p(*rng_) < 0.35) {
            g.mix = static_cast<std::uint8_t>(1 + (byteDist(*rng_) % 7));
        }
        if (p(*rng_) < 0.50) {
            g.mult = static_cast<std::uint8_t>(1 + (byteDist(*rng_) % 23));
        }
        if (p(*rng_) < 0.60) {
            g.xorMask = static_cast<std::uint8_t>(byteDist(*rng_));
        }
        if (p(*rng_) < 0.60) {
            g.add = static_cast<std::uint8_t>(byteDist(*rng_));
        }

        return g;
    }

    double previewNovelty(const Genome& g,
                          const std::vector<std::uint8_t>& memory,
                          std::uint64_t sampleIndex) const {
        double diff = 0.0;
        double energy = 0.0;
        int zc = 0;
        double prev = 0.0;

        for (int i = 0; i < 128; ++i) {
            const std::size_t idx = boundedIndex(ptr_ + static_cast<std::size_t>(i * 19), memorySize_);
            const std::uint8_t m = memory[idx];
            const std::uint32_t t = static_cast<std::uint32_t>(sampleIndex + static_cast<std::uint64_t>(i) * 131ULL + tOffset_);
            const std::uint8_t v = evalGenome(g, t, m);
            const double x = (static_cast<double>(v) - 128.0) / 128.0;

            diff += std::fabs(x - prev);
            energy += x * x;
            if ((x >= 0.0) != (prev >= 0.0)) {
                ++zc;
            }
            prev = x;
        }

        const double d = diff / 128.0;
        const double e = std::sqrt(energy / 128.0);
        const double z = static_cast<double>(zc) / 128.0;
        return d * 0.55 + z * 0.30 + e * 0.15;
    }

    void evolve(const std::vector<std::uint8_t>& memory, std::uint64_t sampleIndex) {
        const double currentScore = (noveltyAccum_ / static_cast<double>(std::max(1, evalInterval_))) +
                                    (static_cast<double>(zcAccum_) / static_cast<double>(std::max(1, evalInterval_))) * 0.7;

        Genome best = population_[active_];
        double bestScore = currentScore;

        for (int i = 0; i < 16; ++i) {
            const Genome cand = mutate(population_[active_]);
            const std::uint64_t sig = signature(cand);
            if (isTabu(sig)) {
                continue;
            }

            const double score = previewNovelty(cand, memory, sampleIndex);
            if (score > bestScore) {
                bestScore = score;
                best = cand;
            }
        }

        active_ = (active_ + 1) % static_cast<int>(population_.size());
        population_[active_] = best;

        const std::uint64_t sig = signature(best);
        tabu_.push_back(sig);
        while (tabu_.size() > 128) {
            tabu_.pop_front();
        }
    }

    std::size_t memorySize_;
    int sampleRate_;
    std::size_t ptr_;
    double p1_;
    std::array<Genome, 8> population_{};
    int active_;
    int evalInterval_;
    int evalCounter_;
    double noveltyAccum_;
    double prevNorm_;
    int zcAccum_;
    std::deque<std::uint64_t> tabu_;
    std::mt19937* rng_;
    std::uint32_t tOffset_;
    double formulaMix_;
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

    registry.registerAlgorithm({
        "fdn_prime_feedback",
        "FDN Prime Feedback",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<FdnPrimeFeedback>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "spectral_freeze_permuter",
        "Spectral Freeze Permuter",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<SpectralFreezePermuter>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "modal_mesh_exciter",
        "Modal Mesh Exciter",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<ModalMeshExciter>(memorySize, sampleRate, rng);
        },
    });

    registry.registerAlgorithm({
        "bytebeat_formula_evolver",
        "Bytebeat Formula Evolver",
        [](std::size_t memorySize, int sampleRate, std::mt19937& rng) {
            return std::make_unique<BytebeatFormulaEvolver>(memorySize, sampleRate, rng);
        },
    });
}

}  // namespace ram_audio
