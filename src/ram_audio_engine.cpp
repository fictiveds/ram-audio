#include "ram_audio_engine.hpp"

#include <algorithm>
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

class TimerSwitchPolicy final : public ISwitchPolicy {
public:
    explicit TimerSwitchPolicy(std::mt19937& rng)
        : rng_(rng) {}

    SwitchDecision decide(const SceneState& /*scene*/,
                          std::size_t activeVoices,
                          int minVoices,
                          int maxVoices,
                          std::uint64_t memorySwitchTimer,
                          std::uint64_t voiceSpawnTimer) override {
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

RamAudioEngine::RamAudioEngine(EngineConfig config, AlgorithmRegistry registry)
    : config_(std::move(config)),
      registry_(std::move(registry)),
      rng_(config_.seed != 0 ? config_.seed : std::random_device{}()),
      switchPolicy_(config_.switchPolicy),
      mixPolicy_(config_.mixPolicy) {
    if (!switchPolicy_) {
        switchPolicy_ = std::make_shared<TimerSwitchPolicy>(rng_);
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

    if (config_.verbose) {
        std::cerr << "[+] Подключено к процессу: " << snapshot.processName
                  << " (PID: " << snapshot.pid << "), считано "
                  << std::fixed << std::setprecision(2)
                  << (static_cast<double>(snapshot.bytes.size()) / 1024.0 / 1024.0)
                  << " MB" << std::endl;
    }

    std::vector<SynthVoice> voices;

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

        if (!preferredId.empty() && registry_.has(preferredId) && isAllowed(preferredId)) {
            algo = registry_.create(preferredId, snapshot.bytes.size(), config_.sampleRate, rng_);
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

    if (!createVoice(true)) {
        error = "Не удалось создать anchor-голос";
        return false;
    }
    createVoice(false);

    auto schedulePidSwitchTimer = [&]() -> std::uint64_t {
        return static_cast<std::uint64_t>(
            randomDouble(rng_, static_cast<double>(config_.memorySwitchMinSec),
                         static_cast<double>(config_.memorySwitchMaxSec)) *
            static_cast<double>(config_.sampleRate));
    };

    auto scheduleVoiceSpawnTimer = [&]() -> std::uint64_t {
        return static_cast<std::uint64_t>(
            randomDouble(rng_, static_cast<double>(config_.voiceSpawnMinSec),
                         static_cast<double>(config_.voiceSpawnMaxSec)) *
            static_cast<double>(config_.sampleRate));
    };

    std::uint64_t nextVoiceTime = static_cast<std::uint64_t>(
        randomDouble(rng_, 1.0, 5.0) * static_cast<double>(config_.sampleRate));
    std::uint64_t pidTimer = schedulePidSwitchTimer();

    double smoothedSample = 0.0;
    double energyEma = 0.0;
    int silenceSamples = 0;
    const double silenceEnergyThreshold = 18000.0;
    const int silenceRecoverySamples = std::max(1, config_.sampleRate / 3);
    AudioTelemetry telemetry(config_.sampleRate, 4096, 512);

    SceneState sceneState;
    sceneState.activePid = snapshot.pid;
    sceneState.activeProcessName = snapshot.processName;

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

        const SwitchDecision decision = switchPolicy_->decide(
            sceneState,
            voices.size(),
            config_.minVoices,
            config_.maxVoices,
            pidTimer,
            nextVoiceTime);

        if (decision.switchMemorySource) {
            std::string switchErr;
            MemorySnapshot newSnapshot = getRandomProcessMemory(switchErr);
            if (!newSnapshot.bytes.empty()) {
                snapshot = std::move(newSnapshot);
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
                              << " MB" << std::endl;
                }
            } else if (config_.verbose && !switchErr.empty()) {
                std::cerr << "\n[!] Смена процесса пропущена: " << switchErr << std::endl;
            }
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
            if (static_cast<int>(voices.size()) < targetVoices) {
                createVoice(false);
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

        smoothedSample = mixPolicy_->mix(sceneState, voiceDescriptors, voiceSamples, smoothedSample);
        if (!std::isfinite(smoothedSample)) {
            smoothedSample = 0.0;
        }

        telemetry.pushSample(smoothedSample, i);
        sceneState.telemetry = telemetry.metrics();

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
                if (!createVoice(false)) {
                    break;
                }
                ++spawned;
            }
            silenceSamples = 0;
            if (config_.verbose) {
                std::cerr << "\n[!] Anti-silence: добавлены голоса для восстановления плотности" << std::endl;
            }
        }

        const std::int16_t sample = clampToInt16(smoothedSample);
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
