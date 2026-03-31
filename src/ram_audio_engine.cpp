#include "ram_audio_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <deque>
#include <unordered_map>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr std::size_t kRegionMinBytes = 4096;
constexpr std::size_t kRegionMaxBytes = 200U * 1024U * 1024U;
constexpr std::size_t kMemoryMinBytes = 50U * 1024U;
constexpr std::size_t kChunkSize = 65536;
constexpr double kPi = 3.14159265358979323846;

double randomDouble(std::mt19937& rng, double minVal, double maxVal) {
    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return dist(rng);
}

int randomIntInclusive(std::mt19937& rng, int minVal, int maxVal) {
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(rng);
}

std::vector<std::string> splitByChar(const std::string& source, char delimiter) {
    std::vector<std::string> out;
    std::string current;
    for (char c : source) {
        if (c == delimiter) {
            if (!current.empty()) {
                out.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

bool parseAddressRange(const std::string& rangeToken, std::uint64_t& start, std::uint64_t& end) {
    const auto parts = splitByChar(rangeToken, '-');
    if (parts.size() != 2) {
        return false;
    }

    try {
        start = std::stoull(parts[0], nullptr, 16);
        end = std::stoull(parts[1], nullptr, 16);
    } catch (...) {
        return false;
    }

    return end >= start;
}

std::string readProcessName(int pid) {
    const std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream comm(commPath);
    if (!comm) {
        return "unknown";
    }
    std::string name;
    std::getline(comm, name);
    if (name.empty()) {
        return "unknown";
    }
    return name;
}

bool isAllZero(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] != 0U) {
            return false;
        }
    }
    return true;
}

double normalizedShannonEntropy(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return 0.0;
    }

    std::array<std::uint64_t, 256> histogram{};
    for (std::uint8_t b : bytes) {
        ++histogram[static_cast<std::size_t>(b)];
    }

    const double total = static_cast<double>(bytes.size());
    if (total <= 0.0) {
        return 0.0;
    }

    double entropy = 0.0;
    for (std::uint64_t count : histogram) {
        if (count == 0) {
            continue;
        }
        const double p = static_cast<double>(count) / total;
        entropy -= p * std::log2(p);
    }

    constexpr double kMaxEntropy = 8.0;
    return std::clamp(entropy / kMaxEntropy, 0.0, 1.0);
}

double sampleTimingSeconds(std::mt19937& rng,
                          const std::string& mode,
                          double minSec,
                          double maxSec,
                          double logSigma,
                          double powerAlpha,
                          double autoChaos) {
    const double lo = std::max(1e-3, minSec);
    const double hi = std::max(lo, maxSec);

    auto uniformSeconds = [&]() {
        return randomDouble(rng, lo, hi);
    };

    if (mode == "uniform") {
        return uniformSeconds();
    }

    auto lognormalSeconds = [&]() {
        const double median = std::sqrt(lo * hi);
        const double sigma = std::clamp(logSigma, 0.05, 2.5);
        const double mu = std::log(std::max(1e-6, median));
        std::lognormal_distribution<double> dist(mu, sigma);

        for (int i = 0; i < 12; ++i) {
            const double v = dist(rng);
            if (v >= lo && v <= hi) {
                return v;
            }
        }
        return std::clamp(dist(rng), lo, hi);
    };

    auto powerlawSeconds = [&]() {
        const double alpha = std::clamp(powerAlpha, 1.05, 3.5);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        const double r = std::max(1e-9, 1.0 - u(rng));
        const double exponent = -1.0 / (alpha - 1.0);
        const double raw = lo * std::pow(r, exponent);
        return std::clamp(raw, lo, hi);
    };

    if (mode == "lognormal") {
        return lognormalSeconds();
    }

    if (mode == "powerlaw") {
        return powerlawSeconds();
    }

    const double chaos = std::clamp(autoChaos, 0.0, 1.0);
    const double wLog = 0.25 + 0.55 * chaos;
    const double wPower = 0.15 + 0.65 * chaos;
    const double wUniform = std::max(0.05, 1.0 - (wLog + wPower) * 0.5);
    std::discrete_distribution<int> pick({wUniform, wLog, wPower});
    const int choice = pick(rng);
    if (choice == 1) {
        return lognormalSeconds();
    }
    if (choice == 2) {
        return powerlawSeconds();
    }
    return uniformSeconds();
}

class TimerSwitchPolicy final : public ISwitchPolicy {
public:
    explicit TimerSwitchPolicy(std::mt19937& rng)
        : rng_(rng) {}

    SwitchDecision decide(const SceneState& /*scene*/,
                          std::size_t activeVoices,
                          int minVoices,
                          int maxVoices,
                          std::uint64_t memorySwitchTimer,
                          std::uint64_t voiceSpawnTimer,
                          bool /*hasSwitchCandidate*/,
                          int /*candidatePid*/,
                          double /*candidateEntropy*/) override {
        SwitchDecision decision;
        decision.switchMemorySource = (memorySwitchTimer == 0);

        if (voiceSpawnTimer == 0) {
            const int minTarget = std::max(1, minVoices);
            const int maxTarget = std::max(minTarget, maxVoices);
            decision.targetVoices = randomIntInclusive(rng_, minTarget, maxTarget);
            decision.spawnVoice = static_cast<int>(activeVoices) < decision.targetVoices;
        }

        return decision;
    }

private:
    std::mt19937& rng_;
};

class EntropyTriggeredSwitchPolicy final : public ISwitchPolicy {
public:
    EntropyTriggeredSwitchPolicy(std::mt19937& rng,
                                 double entropyDeltaUp,
                                 double entropyDeltaDown,
                                 double entropyHysteresis,
                                 int switchCooldownSec,
                                 int sampleRate)
        : rng_(rng),
          entropyDeltaUp_(std::max(1e-6, entropyDeltaUp)),
          entropyDeltaDown_(std::max(1e-6, entropyDeltaDown)),
          entropyHysteresis_(std::max(0.0, entropyHysteresis)),
          switchCooldownSamples_(std::max(0, switchCooldownSec * std::max(1, sampleRate))),
          lastSwitchSample_(0),
          hasLastSwitch_(false),
          highLatched_(false),
          lowLatched_(false) {}

    SwitchDecision decide(const SceneState& scene,
                          std::size_t activeVoices,
                          int minVoices,
                          int maxVoices,
                          std::uint64_t memorySwitchTimer,
                          std::uint64_t voiceSpawnTimer,
                          bool hasSwitchCandidate,
                          int candidatePid,
                          double candidateEntropy) override {
        SwitchDecision decision;

        if (voiceSpawnTimer == 0) {
            const int minTarget = std::max(1, minVoices);
            const int maxTarget = std::max(minTarget, maxVoices);
            decision.targetVoices = randomIntInclusive(rng_, minTarget, maxTarget);
            decision.spawnVoice = static_cast<int>(activeVoices) < decision.targetVoices;
        }

        if (!hasSwitchCandidate || candidatePid < 0) {
            return decision;
        }

        if (candidatePid == scene.activePid) {
            return decision;
        }

        if (memorySwitchTimer != 0) {
            return decision;
        }

        if (hasLastSwitch_) {
            const std::uint64_t sinceLast = scene.sampleIndex - lastSwitchSample_;
            if (sinceLast < static_cast<std::uint64_t>(switchCooldownSamples_)) {
                return decision;
            }
        }

        const double delta = candidateEntropy - scene.memoryEntropy;
        const double highThreshold = entropyDeltaUp_;
        const double lowThreshold = -entropyDeltaDown_;
        const bool highEvent = delta >= highThreshold;
        const bool lowEvent = delta <= lowThreshold;

        if (!highLatched_ && highEvent) {
            decision.switchMemorySource = true;
            highLatched_ = true;
            lowLatched_ = false;
        } else if (!lowLatched_ && lowEvent) {
            decision.switchMemorySource = true;
            lowLatched_ = true;
            highLatched_ = false;
        }

        if (highLatched_ && delta <= (highThreshold - entropyHysteresis_)) {
            highLatched_ = false;
        }
        if (lowLatched_ && delta >= (lowThreshold + entropyHysteresis_)) {
            lowLatched_ = false;
        }

        if (decision.switchMemorySource) {
            lastSwitchSample_ = scene.sampleIndex;
            hasLastSwitch_ = true;
        }

        return decision;
    }

private:
    std::mt19937& rng_;
    double entropyDeltaUp_;
    double entropyDeltaDown_;
    double entropyHysteresis_;
    int switchCooldownSamples_;
    std::uint64_t lastSwitchSample_;
    bool hasLastSwitch_;
    bool highLatched_;
    bool lowLatched_;
};

class SmoothedAverageMixPolicy final : public IMixPolicy {
public:
    double mix(const SceneState& scene,
               const std::vector<VoiceDescriptor>& voices,
               const std::vector<double>& voiceSamples,
               double previousOutput) override {
        (void)voices;

        double mixed = 0.0;
        for (double value : voiceSamples) {
            mixed += value;
        }

        const double filterCutoff = 0.03 + scene.macroMod * 0.2;
        return previousOutput + filterCutoff * (mixed - previousOutput);
    }
};

class AdaptiveBusLimiter final {
public:
    AdaptiveBusLimiter(double targetRms, double ceiling, double maxGain, int sampleRate)
        : targetRms_(std::max(100.0, targetRms)),
          ceiling_(std::clamp(ceiling, 1000.0, 32767.0)),
          maxGain_(std::clamp(maxGain, 0.25, 4.0)),
          minGain_(0.25),
          sampleRate_(std::max(1, sampleRate)),
          gain_(1.0),
          rmsFallback_(targetRms_),
          attackCoeff_(std::exp(-1.0 / (0.012 * static_cast<double>(sampleRate_)))),
          releaseCoeff_(std::exp(-1.0 / (0.250 * static_cast<double>(sampleRate_)))) {}

    double process(double sample, const TelemetryMetrics& telemetry) {
        const double measuredRms = telemetry.valid && telemetry.rms > 1.0
                                       ? telemetry.rms
                                       : std::max(1.0, rmsFallback_);
        const double targetGain = std::clamp(targetRms_ / measuredRms, minGain_, maxGain_);
        const double coeff = targetGain < gain_ ? attackCoeff_ : releaseCoeff_;
        gain_ = coeff * gain_ + (1.0 - coeff) * targetGain;

        const double driven = sample * gain_;
        rmsFallback_ = (rmsFallback_ * 0.999) + (std::fabs(driven) * 0.001);

        if (ceiling_ <= 1.0) {
            return driven;
        }
        return ceiling_ * std::tanh(driven / ceiling_);
    }

private:
    double targetRms_;
    double ceiling_;
    double maxGain_;
    double minGain_;
    int sampleRate_;
    double gain_;
    double rmsFallback_;
    double attackCoeff_;
    double releaseCoeff_;
};

class ProbabilisticCrossfadeSwitchPolicy final : public ISwitchPolicy {
public:
    ProbabilisticCrossfadeSwitchPolicy(std::mt19937& rng,
                                       std::shared_ptr<ISwitchPolicy> basePolicy,
                                       int minSceneTimeSec,
                                       double baseProbability,
                                       double energyWeight,
                                       double noveltyWeight,
                                       double hysteresis,
                                       int sampleRate)
        : rng_(rng),
          basePolicy_(std::move(basePolicy)),
          minSceneSamples_(std::max(0, minSceneTimeSec * std::max(1, sampleRate))),
          baseProbability_(std::clamp(baseProbability, 0.0, 1.0)),
          energyWeight_(std::clamp(energyWeight, 0.0, 1.0)),
          noveltyWeight_(std::clamp(noveltyWeight, 0.0, 1.0)),
          hysteresis_(std::clamp(hysteresis, 0.0, 1.0)),
          switchArmed_(false) {}

    SwitchDecision decide(const SceneState& scene,
                          std::size_t activeVoices,
                          int minVoices,
                          int maxVoices,
                          std::uint64_t memorySwitchTimer,
                          std::uint64_t voiceSpawnTimer,
                          bool hasSwitchCandidate,
                          int candidatePid,
                          double candidateEntropy) override {
        if (!basePolicy_) {
            return {};
        }

        SwitchDecision decision = basePolicy_->decide(scene,
                                                      activeVoices,
                                                      minVoices,
                                                      maxVoices,
                                                      memorySwitchTimer,
                                                      voiceSpawnTimer,
                                                      hasSwitchCandidate,
                                                      candidatePid,
                                                      candidateEntropy);

        if (!decision.switchMemorySource) {
            return decision;
        }

        const std::uint64_t sceneAge = scene.sampleIndex - scene.sceneStartSample;
        if (sceneAge < static_cast<std::uint64_t>(minSceneSamples_)) {
            decision.switchMemorySource = false;
            return decision;
        }

        const double energyNorm = std::clamp(scene.telemetry.rms / 18000.0, 0.0, 1.0);
        const double novelty = scene.telemetry.valid
                                   ? (0.45 * scene.telemetry.spectralFlatness +
                                      0.25 * scene.telemetry.zeroCrossingRate +
                                      0.30 * scene.telemetry.transientDensity)
                                   : 0.25;

        const double probability = std::clamp(
            baseProbability_ +
                energyWeight_ * (1.0 - energyNorm) +
                noveltyWeight_ * (1.0 - novelty),
            0.0,
            1.0);

        const bool shouldArm = probability >= (0.5 + hysteresis_ * 0.5);
        const bool shouldDisarm = probability <= std::max(0.0, 0.5 - hysteresis_ * 0.5);
        if (shouldArm) {
            switchArmed_ = true;
        } else if (shouldDisarm) {
            switchArmed_ = false;
        }

        if (!switchArmed_) {
            decision.switchMemorySource = false;
            return decision;
        }

        std::bernoulli_distribution coin(probability);
        if (!coin(rng_)) {
            decision.switchMemorySource = false;
            return decision;
        }

        switchArmed_ = false;
        return decision;
    }

private:
    std::mt19937& rng_;
    std::shared_ptr<ISwitchPolicy> basePolicy_;
    int minSceneSamples_;
    double baseProbability_;
    double energyWeight_;
    double noveltyWeight_;
    double hysteresis_;
    bool switchArmed_;
};

class NoveltyGuard final {
public:
    NoveltyGuard(double threshold, int historySize, int cooldownSec, int sampleRate)
        : threshold_(std::clamp(threshold, 0.0, 1.0)),
          historySize_(std::max(8, historySize)),
          cooldownSamples_(std::max(0, cooldownSec * std::max(1, sampleRate))),
          lastTriggerSample_(0),
          hasTriggered_(false) {}

    bool shouldRecover(const TelemetryMetrics& m, std::uint64_t sampleIndex) {
        if (!m.valid) {
            return false;
        }

        const Fingerprint fp = makeFingerprint(m);
        const double similarity = maxSimilarity(fp);

        history_.push_back(fp);
        if (static_cast<int>(history_.size()) > historySize_) {
            history_.pop_front();
        }

        if (hasTriggered_) {
            const std::uint64_t since = sampleIndex - lastTriggerSample_;
            if (since < static_cast<std::uint64_t>(cooldownSamples_)) {
                return false;
            }
        }

        if (similarity >= threshold_) {
            hasTriggered_ = true;
            lastTriggerSample_ = sampleIndex;
            return true;
        }

        return false;
    }

private:
    struct Fingerprint {
        double rms;
        double centroid;
        double flatness;
        double zcr;
    };

    static Fingerprint makeFingerprint(const TelemetryMetrics& m) {
        return {
            std::clamp(m.rms / 20000.0, 0.0, 1.0),
            std::clamp(m.spectralCentroidHz / 22050.0, 0.0, 1.0),
            std::clamp(m.spectralFlatness, 0.0, 1.0),
            std::clamp(m.zeroCrossingRate, 0.0, 1.0),
        };
    }

    static double similarity(const Fingerprint& a, const Fingerprint& b) {
        const double dr = std::fabs(a.rms - b.rms);
        const double dc = std::fabs(a.centroid - b.centroid);
        const double df = std::fabs(a.flatness - b.flatness);
        const double dz = std::fabs(a.zcr - b.zcr);
        const double distance = (0.30 * dr) + (0.30 * dc) + (0.20 * df) + (0.20 * dz);
        return std::clamp(1.0 - distance, 0.0, 1.0);
    }

    double maxSimilarity(const Fingerprint& current) const {
        double maxSim = 0.0;
        for (const auto& oldFp : history_) {
            maxSim = std::max(maxSim, similarity(current, oldFp));
        }
        return maxSim;
    }

    double threshold_;
    int historySize_;
    int cooldownSamples_;
    std::uint64_t lastTriggerSample_;
    bool hasTriggered_;
    std::deque<Fingerprint> history_;
};

class HmmWithTabuSelector final {
public:
    explicit HmmWithTabuSelector(int tabuWindow, double noveltyBias)
        : tabuWindow_(std::max(0, tabuWindow)),
          noveltyBias_(std::clamp(noveltyBias, 0.0, 1.0)),
          transitions_(),
          usage_(),
          last_(),
          initialized_(false) {}

    std::string pick(const std::vector<AlgorithmEntry>& entries,
                     const std::vector<std::string>& allowed,
                     std::mt19937& rng) {
        std::vector<std::string> candidates;
        candidates.reserve(entries.size());

        for (const auto& e : entries) {
            if (!allowed.empty() &&
                std::find(allowed.begin(), allowed.end(), e.id) == allowed.end()) {
                continue;
            }
            candidates.push_back(e.id);
            usage_.emplace(e.id, usage_[e.id]);
            transitions_.emplace(e.id, std::unordered_map<std::string, double>());
        }

        if (candidates.empty()) {
            return std::string();
        }

        if (!initialized_) {
            initTransitions(candidates);
            initialized_ = true;
        }

        std::vector<double> weights;
        weights.reserve(candidates.size());
        for (const auto& id : candidates) {
            const bool tabued = std::find(last_.begin(), last_.end(), id) != last_.end();
            double base = tabued ? 0.0 : 1.0;

            if (!last_.empty()) {
                const std::string& prev = last_.back();
                auto rowIt = transitions_.find(prev);
                if (rowIt != transitions_.end()) {
                    auto it = rowIt->second.find(id);
                    if (it != rowIt->second.end()) {
                        base = tabued ? 0.0 : it->second;
                    }
                }
            }

            const double noveltyBonus = 1.0 + noveltyBias_ / (1.0 + static_cast<double>(usage_[id]));
            weights.push_back(base * noveltyBonus);
        }

        double weightSum = 0.0;
        for (double w : weights) {
            weightSum += w;
        }

        std::size_t index = 0;
        if (weightSum <= 1e-12) {
            std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
            index = dist(rng);
        } else {
            std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
            index = dist(rng);
        }

        const std::string picked = candidates[index];
        usage_[picked] += 1;
        last_.push_back(picked);
        while (static_cast<int>(last_.size()) > tabuWindow_) {
            last_.pop_front();
        }
        return picked;
    }

private:
    void initTransitions(const std::vector<std::string>& ids) {
        const double p = 1.0 / static_cast<double>(std::max<std::size_t>(1, ids.size()));
        for (const auto& from : ids) {
            auto& row = transitions_[from];
            for (const auto& to : ids) {
                row[to] = p;
            }
        }
    }

    int tabuWindow_;
    double noveltyBias_;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> transitions_;
    std::unordered_map<std::string, int> usage_;
    std::deque<std::string> last_;
    bool initialized_;
};

class SceneConductor final {
public:
    struct Profile {
        double density = 0.5;
        double spectralTilt = 0.0;
    };

    SceneConductor(int sampleRate,
                   int macroMinSec,
                   int macroMaxSec,
                   int microMinMs,
                   int microMaxMs,
                   std::mt19937& rng)
        : sampleRate_(std::max(1, sampleRate)),
          macroMinSamples_(std::max(1, macroMinSec * std::max(1, sampleRate_))),
          macroMaxSamples_(std::max(macroMinSamples_, macroMaxSec * std::max(1, sampleRate_))),
          microMinSamples_(std::max(1, (microMinMs * std::max(1, sampleRate_)) / 1000)),
          microMaxSamples_(std::max(microMinSamples_, (microMaxMs * std::max(1, sampleRate_)) / 1000)),
          nextMacroAt_(0),
          nextMicroAt_(0),
          profile_(),
          rng_(rng) {
        scheduleMacro(0);
        scheduleMicro(0);
    }

    bool maybeAdvance(std::uint64_t sampleIndex, SceneState& scene, bool verbose) {
        bool changed = false;

        if (sampleIndex >= nextMacroAt_) {
            scene.sceneIndex += 1;
            scene.sceneStartSample = sampleIndex;
            randomizeMacroProfile();
            scheduleMacro(sampleIndex);
            changed = true;

            if (verbose) {
                std::cerr << "\n[+] SceneConductor macro-scene=" << scene.sceneIndex
                          << " density=" << std::fixed << std::setprecision(2) << profile_.density
                          << " tilt=" << std::fixed << std::setprecision(2) << profile_.spectralTilt
                          << std::endl;
            }
        }

        if (sampleIndex >= nextMicroAt_) {
            randomizeMicroNudge();
            scheduleMicro(sampleIndex);
            changed = true;
        }

        scene.macroMod = std::clamp(scene.macroMod * (0.55 + profile_.density * 0.9), 0.0, 1.0);
        return changed;
    }

    const Profile& profile() const {
        return profile_;
    }

private:
    void scheduleMacro(std::uint64_t now) {
        std::uniform_int_distribution<int> dist(macroMinSamples_, macroMaxSamples_);
        nextMacroAt_ = now + static_cast<std::uint64_t>(dist(rng_));
    }

    void scheduleMicro(std::uint64_t now) {
        std::uniform_int_distribution<int> dist(microMinSamples_, microMaxSamples_);
        nextMicroAt_ = now + static_cast<std::uint64_t>(dist(rng_));
    }

    void randomizeMacroProfile() {
        std::uniform_real_distribution<double> d(0.15, 1.0);
        std::uniform_real_distribution<double> t(-0.5, 0.5);
        profile_.density = d(rng_);
        profile_.spectralTilt = t(rng_);
    }

    void randomizeMicroNudge() {
        std::uniform_real_distribution<double> n(-0.06, 0.06);
        profile_.density = std::clamp(profile_.density + n(rng_), 0.10, 1.0);
    }

    int sampleRate_;
    int macroMinSamples_;
    int macroMaxSamples_;
    int microMinSamples_;
    int microMaxSamples_;
    std::uint64_t nextMacroAt_;
    std::uint64_t nextMicroAt_;
    Profile profile_;
    std::mt19937& rng_;
};

class BandSplitMixer final {
public:
    BandSplitMixer(int sampleRate,
                   double baseLowHz,
                   double baseHighHz,
                   double driftHz,
                   bool pinFamilies)
        : sampleRate_(std::max(1, sampleRate)),
          baseLowHz_(std::clamp(baseLowHz, 40.0, 1200.0)),
          baseHighHz_(std::clamp(baseHighHz, 800.0, 12000.0)),
          driftHz_(std::max(0.0, driftHz)),
          pinFamilies_(pinFamilies),
          lowState_(0.0),
          highState_(0.0),
          prevIn_(0.0),
          previousOut_(0.0),
          lfoPhase_(0.0) {
        if (baseLowHz_ >= baseHighHz_) {
            baseLowHz_ = std::min(baseLowHz_, baseHighHz_ * 0.35);
        }
    }

    double process(const SceneState& scene,
                   const std::vector<VoiceDescriptor>& voices,
                   const std::vector<double>& voiceSamples) {
        const double drift = std::sin(lfoPhase_) * driftHz_;
        const double lowHz = std::clamp(baseLowHz_ + drift, 30.0, baseHighHz_ - 40.0);
        const double highHz = std::clamp(baseHighHz_ - drift * 0.65, lowHz + 40.0,
                                         std::min(18000.0, 0.49 * static_cast<double>(sampleRate_)));
        lfoPhase_ += (2.0 * kPi * 0.07) / static_cast<double>(sampleRate_);
        if (lfoPhase_ > 2.0 * kPi) {
            lfoPhase_ -= 2.0 * kPi;
        }

        const double lowAlpha = onePoleAlpha(lowHz);
        const double highAlpha = onePoleAlpha(highHz);

        double in = 0.0;
        if (!voiceSamples.empty()) {
            if (pinFamilies_ && voices.size() == voiceSamples.size()) {
                in = mixPinnedFamilies(voices, voiceSamples, scene.macroMod);
            } else {
                for (double s : voiceSamples) {
                    in += s;
                }
            }
        }

        lowState_ += lowAlpha * (in - lowState_);
        const double hp1 = in - lowState_;
        highState_ = highAlpha * (highState_ + hp1 - prevIn_);
        prevIn_ = hp1;
        const double bandLow = lowState_;
        const double bandHigh = highState_;
        const double bandMid = in - bandLow - bandHigh;

        const double density = std::clamp(scene.macroMod, 0.0, 1.0);
        const double lowGain = 0.80 + 0.35 * density;
        const double midGain = 0.88 + 0.28 * (1.0 - std::fabs(0.5 - density) * 2.0);
        const double highGain = 0.74 + 0.42 * (1.0 - density);

        const double mixed = bandLow * lowGain + bandMid * midGain + bandHigh * highGain;
        const double smooth = 0.06 + 0.16 * density;
        previousOut_ += smooth * (mixed - previousOut_);
        return previousOut_;
    }

private:
    static double familyHash01(const std::string& id) {
        std::uint32_t h = 2166136261u;
        for (char c : id) {
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        return static_cast<double>(h % 10000u) / 9999.0;
    }

    double onePoleAlpha(double cutoffHz) const {
        const double c = std::max(1.0, cutoffHz);
        const double rc = 1.0 / (2.0 * kPi * c);
        const double dt = 1.0 / static_cast<double>(sampleRate_);
        return dt / (rc + dt);
    }

    static double softSign(double x) {
        return x / (1.0 + std::fabs(x));
    }

    double mixPinnedFamilies(const std::vector<VoiceDescriptor>& voices,
                             const std::vector<double>& voiceSamples,
                             double macroMod) const {
        double low = 0.0;
        double mid = 0.0;
        double high = 0.0;

        for (std::size_t i = 0; i < voices.size(); ++i) {
            const double s = voiceSamples[i];
            const double bucket = familyHash01(voices[i].algorithmId);
            if (bucket < 0.34) {
                low += s;
            } else if (bucket < 0.67) {
                mid += s;
            } else {
                high += s;
            }
        }

        const double tilt = softSign((macroMod - 0.5) * 2.0);
        return low * (0.9 + 0.2 * (1.0 - tilt)) +
               mid * 0.85 +
               high * (0.9 + 0.2 * (1.0 + tilt));
    }

    int sampleRate_;
    double baseLowHz_;
    double baseHighHz_;
    double driftHz_;
    bool pinFamilies_;
    double lowState_;
    double highState_;
    double prevIn_;
    double previousOut_;
    double lfoPhase_;
};

class ModulationMatrix final {
public:
    ModulationMatrix(bool enabled,
                     double depth,
                     double feedbackLimit,
                     double wavefoldDepth)
        : enabled_(enabled),
          depth_(std::clamp(depth, 0.0, 1.0)),
          feedbackLimit_(std::clamp(feedbackLimit, 0.0, 0.95)),
          wavefoldDepth_(std::clamp(wavefoldDepth, 0.0, 1.0)),
          feedbackState_(0.0),
          holdCounter_(0) {}

    const std::vector<double>& process(const std::vector<VoiceDescriptor>& voices,
                                       const std::vector<double>& in) {
        if (!enabled_ || in.empty()) {
            scratch_ = in;
            return scratch_;
        }

        const std::size_t n = in.size();
        const int updateStride = chooseUpdateStride(n);
        if (holdCounter_ > 0 && held_.size() == n) {
            --holdCounter_;
            scratch_.resize(n);
            const double wet = 0.55 + depth_ * 0.25;
            for (std::size_t i = 0; i < n; ++i) {
                scratch_[i] = in[i] * (1.0 - wet) + held_[i] * wet;
            }
            return scratch_;
        }

        scratch_.assign(n, 0.0);
        double inEnergy = 0.0;
        for (double s : in) {
            if (std::isfinite(s)) {
                inEnergy += s * s;
            }
        }

        for (std::size_t i = 0; i < n; ++i) {
            const double carrier = in[i];
            if (!std::isfinite(carrier)) {
                scratch_[i] = 0.0;
                continue;
            }

            double mod = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                if (j == i) {
                    continue;
                }

                const double route = routeWeight(voices, i, j);
                mod += in[j] * route;
            }

            const double am = carrier * (1.0 + depth_ * std::tanh(mod * 0.2));
            const double ring = carrier + (depth_ * 0.6) * (carrier * std::tanh(mod));
            const double mixed = 0.55 * am + 0.45 * ring;
            const double shaped = wavefold(mixed, wavefoldDepth_ * (0.2 + depth_ * 0.8));
            scratch_[i] = carrier * (1.0 - depth_ * 0.45) + shaped * (depth_ * 0.45);
        }

        double outEnergy = 0.0;
        for (double v : scratch_) {
            if (std::isfinite(v)) {
                outEnergy += v * v;
            }
        }

        if (inEnergy > 1e-9 && outEnergy > 1e-9) {
            const double makeUp = std::clamp(std::sqrt(inEnergy / outEnergy), 0.35, 8.0);
            for (double& v : scratch_) {
                v *= makeUp;
            }
        }

        double sum = 0.0;
        for (double v : scratch_) {
            sum += v;
        }
        const double mono = sum / static_cast<double>(n);

        feedbackState_ = std::clamp((feedbackState_ * 0.97) + mono * (depth_ * 0.03),
                                    -feedbackLimit_, feedbackLimit_);

        for (double& v : scratch_) {
            v = std::clamp(v + feedbackState_ * 0.3, -32000.0, 32000.0);
        }

        held_ = scratch_;
        holdCounter_ = std::max(0, updateStride - 1);

        return scratch_;
    }

    bool enabled() const {
        return enabled_;
    }

private:
    int chooseUpdateStride(std::size_t voiceCount) const {
        int stride = 1;
        if (depth_ > 0.25) {
            stride = 2;
        }
        if (depth_ > 0.45) {
            stride = 3;
        }
        if (depth_ > 0.65) {
            stride = 4;
        }
        if (voiceCount > 8) {
            stride += 1;
        }
        return stride;
    }

    static double idHashWeight(const std::string& id) {
        std::uint32_t h = 2166136261u;
        for (char c : id) {
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        return 0.5 + (static_cast<double>(h % 1000u) / 1000.0);
    }

    double routeWeight(const std::vector<VoiceDescriptor>& voices,
                       std::size_t i,
                       std::size_t j) const {
        double w = 1.0;
        if (i < voices.size() && j < voices.size()) {
            const double ai = idHashWeight(voices[i].algorithmId);
            const double aj = idHashWeight(voices[j].algorithmId);
            w = 0.2 + 0.8 * std::fabs(ai - aj);
        }
        return std::clamp(w * depth_, 0.0, feedbackLimit_);
    }

    static double wavefold(double x, double amount) {
        if (amount <= 0.0 || !std::isfinite(x)) {
            return std::isfinite(x) ? x : 0.0;
        }

        const double scale = std::max(1.0, std::fabs(x));
        const double xn = x / scale;
        const double drive = 1.0 + amount * 4.0;
        const double folded = std::sin(xn * kPi * drive);
        const double shaped = std::tanh((0.65 * xn + 0.35 * folded) * drive * 0.8);
        const double blended = xn * (1.0 - amount) + shaped * amount;
        return blended * scale;
    }

    bool enabled_;
    double depth_;
    double feedbackLimit_;
    double wavefoldDepth_;
    double feedbackState_;
    int holdCounter_;
    std::vector<double> scratch_;
    std::vector<double> held_;
};

}  // namespace

RamAudioEngine::SynthVoice::SynthVoice(std::unique_ptr<IRamAlgorithm> algorithm,
                                       int sampleRate,
                                       std::mt19937& rng,
                                       bool anchor)
    : algorithm_(std::move(algorithm)),
      sampleRate_(sampleRate),
      volume_(randomDouble(rng, 0.3, 0.9)),
      lifeSamples_(anchor ? std::numeric_limits<int>::max()
                          : static_cast<int>(randomDouble(rng, 5.0, 25.0) * static_cast<double>(sampleRate))),
      age_(0),
      downsample_(randomIntInclusive(rng, 1, 60)),
      holdCount_(0),
      currentValue_(0.0),
      anchor_(anchor) {
    if (anchor_) {
        volume_ = randomDouble(rng, 0.24, 0.5);
        downsample_ = randomIntInclusive(rng, 1, 8);
    } else if (algorithm_ && algorithm_->prefersHighResolution()) {
        const int options[] = {1, 1, 3, 10};
        std::uniform_int_distribution<int> dist(0, 3);
        downsample_ = options[dist(rng)];
    }
}

bool RamAudioEngine::SynthVoice::isDead() const {
    return !anchor_ && age_ >= lifeSamples_;
}

bool RamAudioEngine::SynthVoice::isAnchor() const {
    return anchor_;
}

VoiceDescriptor RamAudioEngine::SynthVoice::descriptor() const {
    VoiceDescriptor desc;
    desc.algorithmId = algorithm_ ? algorithm_->id() : "unknown";
    desc.anchor = anchor_;
    desc.volume = volume_;
    desc.downsample = downsample_;
    desc.ageSamples = age_;
    desc.lifeSamples = lifeSamples_;
    desc.currentValue = currentValue_;
    return desc;
}

double RamAudioEngine::SynthVoice::tick(std::uint64_t sampleIndex,
                                        const std::vector<std::uint8_t>& memory,
                                        double macroMod) {
    ++age_;

    double env = 1.0;
    const double fadeLen = static_cast<double>(sampleRate_) * 2.0;
    if (anchor_) {
        if (static_cast<double>(age_) < fadeLen) {
            env = static_cast<double>(age_) / fadeLen;
        }
    } else if (static_cast<double>(age_) < fadeLen) {
        env = static_cast<double>(age_) / fadeLen;
    } else if (static_cast<double>(lifeSamples_ - age_) < fadeLen) {
        env = static_cast<double>(lifeSamples_ - age_) / fadeLen;
        if (env < 0.0) {
            env = 0.0;
        }
    }

    ++holdCount_;
    if (holdCount_ >= downsample_) {
        holdCount_ = 0;
        if (algorithm_) {
            currentValue_ = algorithm_->generate(sampleIndex, memory, macroMod);
        }
    }

    return currentValue_ * env * volume_;
}

void RamAudioEngine::SynthVoice::onMemorySizeChanged(std::size_t newMemorySize) {
    if (algorithm_) {
        algorithm_->onMemorySizeChanged(newMemorySize);
    }
}

void RamAudioEngine::SynthVoice::applyGenetics(double volume, int downsample, int lifeSamples) {
    volume_ = std::clamp(volume, 0.08, 1.0);
    downsample_ = std::max(1, std::min(80, downsample));
    if (!anchor_) {
        lifeSamples_ = std::max(sampleRate_ / 2, lifeSamples);
    }
}

RamAudioEngine::RamAudioEngine(EngineConfig config, AlgorithmRegistry registry)
    : config_(std::move(config)),
      registry_(std::move(registry)),
      rng_(config_.seed != 0 ? config_.seed : std::random_device{}()),
      switchPolicy_(config_.switchPolicy),
      mixPolicy_(config_.mixPolicy) {
    if (!switchPolicy_) {
        std::shared_ptr<ISwitchPolicy> baseSwitch;
        if (config_.switchMode == "entropy-triggered") {
            baseSwitch = std::make_shared<EntropyTriggeredSwitchPolicy>(
                rng_,
                config_.entropyDeltaUp,
                config_.entropyDeltaDown,
                config_.entropyHysteresis,
                config_.switchCooldownSec,
                config_.sampleRate);
        } else {
            baseSwitch = std::make_shared<TimerSwitchPolicy>(rng_);
        }

        switchPolicy_ = std::make_shared<ProbabilisticCrossfadeSwitchPolicy>(
            rng_,
            baseSwitch,
            config_.minSceneTimeSec,
            config_.switchProbBase,
            config_.switchProbEnergyWeight,
            config_.switchProbNoveltyWeight,
            config_.switchProbHysteresis,
            config_.sampleRate);
    }
    if (!mixPolicy_) {
        mixPolicy_ = std::make_shared<SmoothedAverageMixPolicy>();
    }
}

bool RamAudioEngine::run(OutputSink& sink, RunStats& stats, std::string& error) {
    if (config_.sampleRate <= 1000) {
        error = "Частота дискретизации слишком низкая";
        return false;
    }
    if (!config_.infinite && config_.durationSec <= 0) {
        error = "Длительность должна быть больше нуля";
        return false;
    }
    if (config_.maxMemoryBytes < kMemoryMinBytes) {
        error = "Лимит памяти слишком мал";
        return false;
    }
    if (registry_.entries().empty()) {
        error = "Реестр алгоритмов пуст";
        return false;
    }

    MemorySnapshot snapshot = getRandomProcessMemory(error);
    if (snapshot.bytes.empty()) {
        if (error.empty()) {
            error = "Не удалось получить память процесса";
        }
        return false;
    }

    stats.memorySizeBytes = snapshot.bytes.size();
    stats.pid = snapshot.pid;
    stats.processName = snapshot.processName;
    double memoryEntropy = normalizedShannonEntropy(snapshot.bytes);

    if (config_.verbose) {
        std::cerr << "[+] Подключено к процессу: " << snapshot.processName
                  << " (PID: " << snapshot.pid << "), считано "
                  << std::fixed << std::setprecision(2)
                  << (static_cast<double>(snapshot.bytes.size()) / 1024.0 / 1024.0)
                  << " MB" << std::endl;
        std::cerr << "[+] Timing mode: " << config_.timingMode
                  << " (log-sigma=" << config_.timingLogSigma
                  << ", power-alpha=" << config_.timingPowerAlpha
                  << ", auto-chaos=" << config_.timingAutoChaos << ")"
                  << std::endl;
    }

    std::vector<SynthVoice> voices;
    HmmWithTabuSelector hmmSelector(config_.hmmTabuWindow, config_.hmmNoveltyBias);
    SceneConductor conductor(config_.sampleRate,
                             config_.sceneMacroMinSec,
                             config_.sceneMacroMaxSec,
                             config_.sceneMicroMinMs,
                             config_.sceneMicroMaxMs,
                             rng_);

    auto isAllowed = [&](const std::string& id) {
        if (config_.allowedAlgorithmIds.empty()) {
            return true;
        }
        return std::find(config_.allowedAlgorithmIds.begin(),
                         config_.allowedAlgorithmIds.end(),
                         id) != config_.allowedAlgorithmIds.end();
    };

    auto createVoice = [&](bool anchor, const std::string& preferredId = std::string()) -> bool {
        std::unique_ptr<IRamAlgorithm> algo;
        std::string selectedId = preferredId;

        if (!selectedId.empty() && registry_.has(selectedId) && isAllowed(selectedId)) {
            algo = registry_.create(selectedId, snapshot.bytes.size(), config_.sampleRate, rng_);
        }

        if (!algo && anchor) {
            static const char* kAnchorCandidates[] = {
                "chaotic_lorenz_fm",
                "memory_phase_modulation",
                "hilbert_drone",
                "wavefolding_delta_bass",
                "bytebeat_processor",
            };

            for (const char* id : kAnchorCandidates) {
                if (!isAllowed(id) || !registry_.has(id)) {
                    continue;
                }
                algo = registry_.create(id, snapshot.bytes.size(), config_.sampleRate, rng_);
                if (algo) {
                    break;
                }
            }
        }

        if (!algo) {
            selectedId = hmmSelector.pick(registry_.entries(), config_.allowedAlgorithmIds, rng_);
            if (!selectedId.empty() && registry_.has(selectedId) && isAllowed(selectedId)) {
                algo = registry_.create(selectedId,
                                        snapshot.bytes.size(),
                                        config_.sampleRate,
                                        rng_);
            }
        }

        if (!algo) {
            algo = registry_.createRandom(config_.allowedAlgorithmIds,
                                          snapshot.bytes.size(),
                                          config_.sampleRate,
                                          rng_);
        }

        if (!algo) {
            return false;
        }

        voices.emplace_back(std::move(algo), config_.sampleRate, rng_, anchor);
        return true;
    };

    auto spawnGeneticVoice = [&]() -> bool {
        std::vector<VoiceDescriptor> pool;
        pool.reserve(voices.size());
        for (const auto& voice : voices) {
            const VoiceDescriptor d = voice.descriptor();
            if (!d.anchor) {
                pool.push_back(d);
            }
        }

        if (pool.size() < 2) {
            return createVoice(false);
        }

        std::uniform_int_distribution<std::size_t> parentDist(0, pool.size() - 1);
        const std::size_t parentA = parentDist(rng_);
        std::size_t parentB = parentDist(rng_);
        while (parentB == parentA && pool.size() > 1) {
            parentB = parentDist(rng_);
        }

        const VoiceDescriptor& p1 = pool[parentA];
        const VoiceDescriptor& p2 = pool[parentB];

        std::string childAlgorithm = randomDouble(rng_, 0.0, 1.0) < 0.5
                                         ? p1.algorithmId
                                         : p2.algorithmId;

        if (randomDouble(rng_, 0.0, 1.0) < config_.geneticAlgorithmMutation) {
            const std::string mutated = hmmSelector.pick(registry_.entries(), config_.allowedAlgorithmIds, rng_);
            if (!mutated.empty() && isAllowed(mutated)) {
                childAlgorithm = mutated;
            }
        }

        if (!createVoice(false, childAlgorithm)) {
            return false;
        }

        double childVolume = (p1.volume + p2.volume) * 0.5;
        int childDownsample = std::max(1, static_cast<int>(
            std::lround((static_cast<double>(p1.downsample + p2.downsample)) * 0.5)));
        const int p1Life = std::max(config_.sampleRate / 2, p1.lifeSamples);
        const int p2Life = std::max(config_.sampleRate / 2, p2.lifeSamples);
        int childLife = std::max(
            config_.sampleRate / 2,
            static_cast<int>((static_cast<long long>(p1Life) + static_cast<long long>(p2Life)) / 2LL));

        if (randomDouble(rng_, 0.0, 1.0) < config_.geneticMutationRate) {
            const double jitter = randomDouble(rng_, -0.45, 0.45) * config_.geneticMutationDepth;
            childVolume *= (1.0 + jitter);
        }
        if (randomDouble(rng_, 0.0, 1.0) < config_.geneticMutationRate) {
            const int jitter = static_cast<int>(
                std::lround(randomDouble(rng_, -16.0, 16.0) * config_.geneticMutationDepth));
            childDownsample += jitter;
        }
        if (randomDouble(rng_, 0.0, 1.0) < config_.geneticMutationRate) {
            const double lifeSecJitter = randomDouble(rng_, -10.0, 10.0) * config_.geneticMutationDepth;
            childLife += static_cast<int>(lifeSecJitter * static_cast<double>(config_.sampleRate));
        }

        voices.back().applyGenetics(childVolume, childDownsample, childLife);
        return true;
    };

    if (!createVoice(true)) {
        error = "Не удалось создать anchor-голос";
        return false;
    }
    createVoice(false);

    auto schedulePidSwitchTimer = [&]() -> std::uint64_t {
        return static_cast<std::uint64_t>(
            sampleTimingSeconds(rng_,
                                config_.timingMode,
                                static_cast<double>(config_.memorySwitchMinSec),
                                static_cast<double>(config_.memorySwitchMaxSec),
                                config_.timingLogSigma,
                                config_.timingPowerAlpha,
                                config_.timingAutoChaos) *
            static_cast<double>(config_.sampleRate));
    };

    auto scheduleVoiceSpawnTimer = [&]() -> std::uint64_t {
        return static_cast<std::uint64_t>(
            sampleTimingSeconds(rng_,
                                config_.timingMode,
                                static_cast<double>(config_.voiceSpawnMinSec),
                                static_cast<double>(config_.voiceSpawnMaxSec),
                                config_.timingLogSigma,
                                config_.timingPowerAlpha,
                                config_.timingAutoChaos) *
            static_cast<double>(config_.sampleRate));
    };

    std::uint64_t nextVoiceTime = static_cast<std::uint64_t>(
        sampleTimingSeconds(rng_,
                            config_.timingMode,
                            1.0,
                            5.0,
                            config_.timingLogSigma,
                            config_.timingPowerAlpha,
                            config_.timingAutoChaos) *
        static_cast<double>(config_.sampleRate));
    std::uint64_t pidTimer = schedulePidSwitchTimer();

    double smoothedSample = 0.0;
    double energyEma = 0.0;
    int silenceSamples = 0;
    const double silenceEnergyThreshold = 18000.0;
    const int silenceRecoverySamples = std::max(1, config_.sampleRate / 3);
    AudioTelemetry telemetry(config_.sampleRate, 4096, 512);
    AdaptiveBusLimiter busLimiter(config_.targetRms,
                                  config_.limiterCeiling,
                                  config_.limiterMaxGain,
                                  config_.sampleRate);
    NoveltyGuard noveltyGuard(config_.noveltyThreshold,
                              config_.noveltyHistory,
                              config_.noveltyCooldownSec,
                              config_.sampleRate);
    BandSplitMixer bandMixer(config_.sampleRate,
                             config_.bandSplitLowHz,
                             config_.bandSplitHighHz,
                             config_.bandSplitDriftHz,
                             config_.bandPinFamilies);
    ModulationMatrix modMatrix(config_.modulationMatrixEnable,
                               config_.modulationMatrixDepth,
                               config_.modulationFeedbackLimit,
                               config_.modulationWavefoldDepth);

    const int crossfadeSamples = std::max(
        0,
        static_cast<int>((static_cast<long long>(config_.crossfadeMs) * config_.sampleRate) / 1000));
    std::vector<double> crossfadeTail;
    std::size_t crossfadeTailPos = 0;
    if (crossfadeSamples > 0) {
        crossfadeTail.assign(static_cast<std::size_t>(crossfadeSamples), 0.0);
    }

    SceneState sceneState;
    sceneState.activePid = snapshot.pid;
    sceneState.activeProcessName = snapshot.processName;
    sceneState.memoryEntropy = memoryEntropy;

    const std::uint64_t totalSamples = config_.infinite
                                           ? std::numeric_limits<std::uint64_t>::max()
                                           : static_cast<std::uint64_t>(config_.durationSec) *
                                                 static_cast<std::uint64_t>(config_.sampleRate);

    for (std::uint64_t i = 0; i < totalSamples; ++i) {
        if (config_.stopFlag != nullptr && *config_.stopFlag != 0) {
            break;
        }

        const double macroMod = (lfo(i, 0.05, config_.sampleRate) + 1.0) / 2.0;
        sceneState.sampleIndex = i;
        sceneState.macroMod = macroMod;
        sceneState.activePid = snapshot.pid;
        sceneState.activeProcessName = snapshot.processName;
        sceneState.memoryEntropy = memoryEntropy;

        const bool sceneChanged = conductor.maybeAdvance(i, sceneState, config_.verbose);
        if (sceneChanged && sceneState.sceneStartSample == i && config_.verbose) {
            std::cerr << "[>] Scene event sample=" << i
                      << " pid=" << sceneState.activePid
                      << " process=" << sceneState.activeProcessName
                      << std::endl;
        }

        MemorySnapshot switchCandidate;
        bool hasSwitchCandidate = false;
        int candidatePid = -1;
        double candidateEntropy = memoryEntropy;
        std::string switchCandidateError;
        if (pidTimer == 0) {
            switchCandidate = getRandomProcessMemory(switchCandidateError);
            if (!switchCandidate.bytes.empty()) {
                hasSwitchCandidate = true;
                candidatePid = switchCandidate.pid;
                candidateEntropy = normalizedShannonEntropy(switchCandidate.bytes);
            }
        }

        const SwitchDecision decision = switchPolicy_->decide(
            sceneState,
            voices.size(),
            config_.minVoices,
            config_.maxVoices,
            pidTimer,
            nextVoiceTime,
            hasSwitchCandidate,
            candidatePid,
            candidateEntropy);

        if (decision.switchMemorySource) {
            if (hasSwitchCandidate) {
                const double prevEntropy = memoryEntropy;

                if (crossfadeSamples > 0) {
                    const std::size_t tailSize = static_cast<std::size_t>(crossfadeSamples);
                    for (std::size_t k = 0; k < tailSize; ++k) {
                        const std::size_t idx = (crossfadeTailPos + tailSize - 1U - k) % tailSize;
                        crossfadeTail[k] = smoothedSample;
                        (void)idx;
                    }
                    crossfadeTailPos = 0;
                }

                snapshot = std::move(switchCandidate);
                memoryEntropy = candidateEntropy;
                stats.memorySizeBytes = snapshot.bytes.size();
                stats.pid = snapshot.pid;
                stats.processName = snapshot.processName;
                for (auto& voice : voices) {
                    voice.onMemorySizeChanged(snapshot.bytes.size());
                }
                sceneState.sceneIndex += 1;
                sceneState.sceneStartSample = i;
                sceneState.activePid = snapshot.pid;
                sceneState.activeProcessName = snapshot.processName;
                if (config_.verbose) {
                    std::cerr << "\n[+] Смена источника: " << snapshot.processName
                              << " (PID: " << snapshot.pid << "), "
                              << std::fixed << std::setprecision(2)
                              << (static_cast<double>(snapshot.bytes.size()) / 1024.0 / 1024.0)
                              << " MB"
                              << ", entropy " << std::setprecision(4) << prevEntropy
                              << " -> " << memoryEntropy << std::endl;
                }
            } else if (config_.verbose && !switchCandidateError.empty()) {
                std::cerr << "\n[!] Смена процесса пропущена: " << switchCandidateError << std::endl;
            }
        }

        if (pidTimer == 0) {
            pidTimer = schedulePidSwitchTimer();
        }
        if (pidTimer > 0) {
            --pidTimer;
        }

        if (decision.spawnVoice) {
            const int targetVoices = decision.targetVoices > 0
                                         ? decision.targetVoices
                                         : std::max(
                                               std::max(1, config_.minVoices),
                                               static_cast<int>(voices.size()) + 1);
            const double sceneDensity = conductor.profile().density;
            const int adaptiveTarget = std::max(
                std::max(1, config_.minVoices),
                static_cast<int>(std::round(static_cast<double>(targetVoices) * (0.6 + sceneDensity * 0.8))));
            if (static_cast<int>(voices.size()) < adaptiveTarget) {
                spawnGeneticVoice();
            }
        }
        if (nextVoiceTime == 0) {
            nextVoiceTime = scheduleVoiceSpawnTimer();
        }
        if (nextVoiceTime > 0) {
            --nextVoiceTime;
        }

        std::vector<VoiceDescriptor> voiceDescriptors;
        std::vector<double> voiceSamples;
        std::vector<SynthVoice> aliveVoices;
        voiceDescriptors.reserve(voices.size());
        voiceSamples.reserve(voices.size());
        aliveVoices.reserve(voices.size());
        for (auto& voice : voices) {
            const double voiceSample = voice.tick(i, snapshot.bytes, macroMod);
            voiceDescriptors.push_back(voice.descriptor());
            voiceSamples.push_back(voiceSample);
            if (!voice.isDead()) {
                aliveVoices.emplace_back(std::move(voice));
            }
        }
        voices = std::move(aliveVoices);

        bool hasAnchor = false;
        for (const auto& voice : voices) {
            if (voice.isAnchor()) {
                hasAnchor = true;
                break;
            }
        }
        if (!hasAnchor) {
            createVoice(true);
        }
        if (voices.empty()) {
            createVoice(true);
            createVoice(false);
        }

        const std::vector<double>& modulatedSamples = modMatrix.process(voiceDescriptors, voiceSamples);
        smoothedSample = mixPolicy_->mix(sceneState, voiceDescriptors, modulatedSamples, smoothedSample);
        smoothedSample = bandMixer.process(sceneState, voiceDescriptors, modulatedSamples);
        if (!std::isfinite(smoothedSample)) {
            smoothedSample = 0.0;
        }

        if (crossfadeSamples > 0 && !crossfadeTail.empty() && crossfadeTailPos < crossfadeTail.size()) {
            const double t = static_cast<double>(crossfadeTailPos + 1) /
                             static_cast<double>(crossfadeTail.size());
            const double fadeIn = t;
            const double fadeOut = 1.0 - t;
            smoothedSample = smoothedSample * fadeIn + crossfadeTail[crossfadeTailPos] * fadeOut;
            ++crossfadeTailPos;
        }

        telemetry.pushSample(smoothedSample, i);
        sceneState.telemetry = telemetry.metrics();

        if (modMatrix.enabled() && sceneState.telemetry.valid && sceneState.telemetry.rms < 15.0) {
            if (config_.verbose && (i % static_cast<std::uint64_t>(config_.sampleRate) == 0ULL)) {
                std::cerr << "\n[!] Mod-matrix fallback: low RMS detected, injecting dry recovery" << std::endl;
            }
            const double dryRecovery = mixPolicy_->mix(sceneState, voiceDescriptors, voiceSamples, smoothedSample);
            smoothedSample = 0.7 * dryRecovery + 0.3 * smoothedSample;
        }

        if (noveltyGuard.shouldRecover(sceneState.telemetry, i)) {
            int spawned = 0;
            while (spawned < config_.noveltySpawnExtra) {
                if (!spawnGeneticVoice()) {
                    break;
                }
                ++spawned;
            }

            if (config_.verbose) {
                std::cerr << "\n[!] Novelty-guard: similarity threshold exceeded, recovery spawn="
                          << spawned << std::endl;
            }
        }

        const double currentEnergy = smoothedSample * smoothedSample;
        energyEma = (energyEma * 0.9992) + (currentEnergy * 0.0008);
        if (energyEma < silenceEnergyThreshold) {
            ++silenceSamples;
        } else {
            silenceSamples = 0;
        }

        if (silenceSamples >= silenceRecoverySamples) {
            int spawned = 0;
            while (spawned < 2) {
                if (!spawnGeneticVoice()) {
                    break;
                }
                ++spawned;
            }
            silenceSamples = 0;
            if (config_.verbose) {
                std::cerr << "\n[!] Anti-silence: добавлены голоса для восстановления плотности" << std::endl;
            }
        }

        const double limitedSample = busLimiter.process(smoothedSample, sceneState.telemetry);
        const std::int16_t sample = clampToInt16(limitedSample);
        if (!sink.writeSample(sample)) {
            if (config_.stopFlag != nullptr && *config_.stopFlag != 0) {
                break;
            }
            error = "Ошибка записи сэмпла в поток вывода";
            return false;
        }
        ++stats.samplesGenerated;

        if (config_.verbose && i > 0 && (i % static_cast<std::uint64_t>(config_.sampleRate) == 0ULL)) {
            std::cerr << "Синтез... " << (i / static_cast<std::uint64_t>(config_.sampleRate))
                      << " / " << (config_.infinite ? "∞" : std::to_string(config_.durationSec))
                      << " сек. [Голосов: " << voices.size() << "]\r";
            std::cerr.flush();
        }
    }

    if (!sink.finalize()) {
        if (config_.stopFlag != nullptr && *config_.stopFlag != 0) {
            return true;
        }
        error = "Ошибка финализации потока вывода";
        return false;
    }

    if (config_.verbose) {
        std::cerr << std::endl;
    }

    return true;
}

MemorySnapshot RamAudioEngine::getRandomProcessMemory(std::string& error) {
    const std::vector<int> pids = getAllPids();
    if (pids.empty()) {
        error = "В /proc не найдено PID";
        return {};
    }

    std::uniform_int_distribution<std::size_t> pidDist(0, pids.size() - 1);
    const std::size_t maxAttempts = std::min<std::size_t>(pids.size() * 2, 800);

    std::string lastError;
    for (std::size_t attempt = 0; attempt < maxAttempts; ++attempt) {
        const int pid = pids[pidDist(rng_)];
        const std::string name = readProcessName(pid);

        std::string localError;
        MemorySnapshot snapshot = getProcessMemory(pid, name, localError);
        if (snapshot.bytes.size() > kMemoryMinBytes) {
            return snapshot;
        }

        if (!localError.empty()) {
            lastError = localError;
            if (localError.find("доступ") != std::string::npos) {
                break;
            }
        }
    }

    if (!lastError.empty()) {
        error = lastError;
    } else {
        error = "Не найден подходящий процесс с доступной памятью";
    }
    return {};
}

MemorySnapshot RamAudioEngine::getProcessMemory(int pid,
                                                const std::string& processName,
                                                std::string& error) {
    const std::string mapsPath = "/proc/" + std::to_string(pid) + "/maps";
    const std::string memPath = "/proc/" + std::to_string(pid) + "/mem";

    std::ifstream maps(mapsPath);
    if (!maps.is_open()) {
        return {};
    }

    const int memFd = ::open(memPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (memFd < 0) {
        if (errno == EACCES || errno == EPERM) {
            error = "Ошибка доступа к /proc/*/mem, запустите с sudo/root";
        }
        return {};
    }

    MemorySnapshot snapshot;
    snapshot.pid = pid;
    snapshot.processName = processName;
    snapshot.bytes.reserve(std::min<std::size_t>(config_.maxMemoryBytes, 4U * 1024U * 1024U));

    std::vector<std::uint8_t> buffer(kChunkSize);

    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream iss(line);
        std::string rangeToken;
        std::string perms;
        if (!(iss >> rangeToken >> perms)) {
            continue;
        }
        if (perms.find('r') == std::string::npos) {
            continue;
        }

        std::uint64_t startAddr = 0;
        std::uint64_t endAddr = 0;
        if (!parseAddressRange(rangeToken, startAddr, endAddr)) {
            continue;
        }

        const std::uint64_t regionSize = endAddr - startAddr;
        if (regionSize < kRegionMinBytes || regionSize > kRegionMaxBytes) {
            continue;
        }

        for (std::uint64_t offset = 0; offset < regionSize; offset += kChunkSize) {
            const std::size_t toRead = static_cast<std::size_t>(
                std::min<std::uint64_t>(kChunkSize, regionSize - offset));

            const ssize_t bytesRead = ::pread(memFd, buffer.data(), toRead,
                                              static_cast<off_t>(startAddr + offset));
            if (bytesRead <= 0) {
                break;
            }

            const std::size_t chunkBytes = static_cast<std::size_t>(bytesRead);
            if (!isAllZero(buffer.data(), chunkBytes)) {
                snapshot.bytes.insert(snapshot.bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(chunkBytes));
                if (snapshot.bytes.size() > config_.maxMemoryBytes) {
                    break;
                }
            }
        }

        if (snapshot.bytes.size() > config_.maxMemoryBytes) {
            break;
        }
    }

    ::close(memFd);
    return snapshot;
}

std::vector<int> RamAudioEngine::getAllPids() const {
    std::vector<int> pids;

    DIR* procDir = opendir("/proc");
    if (procDir == nullptr) {
        return pids;
    }

    while (true) {
        struct dirent* entry = readdir(procDir);
        if (entry == nullptr) {
            break;
        }

        const char* name = entry->d_name;
        bool allDigits = true;
        for (std::size_t i = 0; name[i] != '\0'; ++i) {
            if (name[i] < '0' || name[i] > '9') {
                allDigits = false;
                break;
            }
        }

        if (!allDigits) {
            continue;
        }

        try {
            pids.push_back(std::stoi(name));
        } catch (...) {
            continue;
        }
    }

    closedir(procDir);
    return pids;
}

std::int16_t RamAudioEngine::clampToInt16(double value) {
    if (value > static_cast<double>(std::numeric_limits<std::int16_t>::max())) {
        return std::numeric_limits<std::int16_t>::max();
    }
    if (value < static_cast<double>(std::numeric_limits<std::int16_t>::min())) {
        return std::numeric_limits<std::int16_t>::min();
    }
    return static_cast<std::int16_t>(value);
}

double RamAudioEngine::lfo(std::uint64_t phase, double freq, int sampleRate) {
    const double x = 2.0 * kPi * freq * static_cast<double>(phase) / static_cast<double>(sampleRate);
    return std::sin(x);
}
