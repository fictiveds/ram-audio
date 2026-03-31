#ifndef RAM_AUDIO_ENGINE_HPP
#define RAM_AUDIO_ENGINE_HPP

#include "algorithms.hpp"
#include "telemetry.hpp"

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

enum class OutputMode {
    File,
    Stream
};

struct VoiceDescriptor {
    std::string algorithmId;
    bool anchor = false;
    double volume = 0.0;
    int ageSamples = 0;
    int lifeSamples = 0;
    double currentValue = 0.0;
};

struct SceneState {
    std::uint64_t sampleIndex = 0;
    std::uint64_t sceneStartSample = 0;
    int sceneIndex = 0;
    int activePid = -1;
    std::string activeProcessName;
    double macroMod = 0.0;
    double memoryEntropy = 0.0;
    TelemetryMetrics telemetry;
};

struct SwitchDecision {
    bool switchMemorySource = false;
    bool spawnVoice = false;
    int targetVoices = 0;
};

class ISwitchPolicy {
public:
    virtual ~ISwitchPolicy() = default;

    virtual SwitchDecision decide(const SceneState& scene,
                                  std::size_t activeVoices,
                                  int minVoices,
                                  int maxVoices,
                                  std::uint64_t memorySwitchTimer,
                                  std::uint64_t voiceSpawnTimer,
                                  bool hasSwitchCandidate,
                                  int candidatePid,
                                  double candidateEntropy) = 0;
};

class IMixPolicy {
public:
    virtual ~IMixPolicy() = default;

    virtual double mix(const SceneState& scene,
                       const std::vector<VoiceDescriptor>& voices,
                       const std::vector<double>& voiceSamples,
                       double previousOutput) = 0;
};

struct EngineConfig {
    int durationSec = 180;
    bool infinite = false;
    int sampleRate = 44100;
    std::size_t maxMemoryBytes = 60U * 1024U * 1024U;
    int minVoices = 2;
    int maxVoices = 6;
    int memorySwitchMinSec = 15;
    int memorySwitchMaxSec = 40;
    int voiceSpawnMinSec = 2;
    int voiceSpawnMaxSec = 8;
    double entropyDeltaUp = 0.015;
    double entropyDeltaDown = 0.015;
    double entropyHysteresis = 0.004;
    int switchCooldownSec = 2;
    unsigned int seed = 0;
    bool verbose = true;
    const volatile std::sig_atomic_t* stopFlag = nullptr;
    std::vector<std::string> allowedAlgorithmIds;
    std::string switchMode = "timer";
    std::string mixMode = "smoothed";
    std::shared_ptr<ISwitchPolicy> switchPolicy;
    std::shared_ptr<IMixPolicy> mixPolicy;
};

struct RunStats {
    std::size_t samplesGenerated = 0;
    std::size_t memorySizeBytes = 0;
    std::string processName;
    int pid = -1;
};

struct MemorySnapshot {
    std::vector<std::uint8_t> bytes;
    int pid = -1;
    std::string processName;
};

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual bool writeSample(std::int16_t sample) = 0;
    virtual bool finalize() = 0;
};

class RamAudioEngine {
public:
    RamAudioEngine(EngineConfig config, AlgorithmRegistry registry);

    bool run(OutputSink& sink, RunStats& stats, std::string& error);

private:
    class SynthVoice {
    public:
        SynthVoice(std::unique_ptr<IRamAlgorithm> algorithm,
                   int sampleRate,
                   std::mt19937& rng,
                   bool anchor);

        bool isDead() const;
        bool isAnchor() const;
        VoiceDescriptor descriptor() const;
        double tick(std::uint64_t sampleIndex,
                    const std::vector<std::uint8_t>& memory,
                    double macroMod);
        void onMemorySizeChanged(std::size_t newMemorySize);

    private:
        std::unique_ptr<IRamAlgorithm> algorithm_;
        int sampleRate_;
        double volume_;
        int lifeSamples_;
        int age_;
        int downsample_;
        int holdCount_;
        double currentValue_;
        bool anchor_;
    };

    MemorySnapshot getRandomProcessMemory(std::string& error);
    MemorySnapshot getProcessMemory(int pid, const std::string& processName, std::string& error);
    std::vector<int> getAllPids() const;

    static std::int16_t clampToInt16(double value);
    static double lfo(std::uint64_t phase, double freq, int sampleRate);

    EngineConfig config_;
    AlgorithmRegistry registry_;
    std::mt19937 rng_;
    std::shared_ptr<ISwitchPolicy> switchPolicy_;
    std::shared_ptr<IMixPolicy> mixPolicy_;
};

#endif
