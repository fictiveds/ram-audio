#include "algorithms.hpp"
#include "audio_io.hpp"
#include "ram_audio_engine.hpp"

#include <algorithm>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct CliOptions {
    OutputMode mode = OutputMode::File;
    std::string outputPath = "real_ram_symphony.wav";
    int durationSec = 180;
    bool infinite = false;
    int sampleRate = 44100;
    std::size_t bufferMs = 500;
    std::size_t maxMemoryMb = 60;
    int minVoices = 2;
    int maxVoices = 6;
    int memorySwitchMinSec = 15;
    int memorySwitchMaxSec = 40;
    int voiceSpawnMinSec = 2;
    int voiceSpawnMaxSec = 8;
    std::string timingMode = "uniform";
    double timingLogSigma = 0.60;
    double timingPowerAlpha = 1.80;
    double timingAutoChaos = 0.55;
    double geneticMutationRate = 0.28;
    double geneticMutationDepth = 0.35;
    double geneticAlgorithmMutation = 0.18;
    std::string switchMode = "timer";
    std::string mixMode = "smoothed";
    double entropyDeltaUp = 0.015;
    double entropyDeltaDown = 0.015;
    double entropyHysteresis = 0.004;
    int switchCooldownSec = 2;
    int sceneMacroMinSec = 30;
    int sceneMacroMaxSec = 180;
    int sceneMicroMinMs = 300;
    int sceneMicroMaxMs = 4000;
    double targetRms = 9000.0;
    double limiterCeiling = 28000.0;
    double limiterMaxGain = 1.8;
    int minSceneTimeSec = 8;
    int crossfadeMs = 140;
    double switchProbBase = 0.22;
    double switchProbEnergyWeight = 0.28;
    double switchProbNoveltyWeight = 0.36;
    double switchProbHysteresis = 0.08;
    int hmmTabuWindow = 3;
    double hmmNoveltyBias = 0.22;
    double noveltyThreshold = 0.93;
    int noveltyHistory = 48;
    int noveltyCooldownSec = 6;
    int noveltySpawnExtra = 2;
    double bandSplitLowHz = 220.0;
    double bandSplitHighHz = 2600.0;
    double bandSplitDriftHz = 90.0;
    bool bandPinFamilies = false;
    unsigned int seed = 0;
    bool verbose = true;
    bool listAlgorithms = false;
    std::vector<std::string> algorithms;
};

bool isFlag(const std::string& arg, const std::string& longName, const std::string& shortName = "") {
    return arg == longName || (!shortName.empty() && arg == shortName);
}

bool parseInt(const std::string& value, int& out) {
    try {
        std::size_t pos = 0;
        int parsed = std::stoi(value, &pos, 10);
        if (pos != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseDouble(const std::string& value, double& out) {
    try {
        std::size_t pos = 0;
        double parsed = std::stod(value, &pos);
        if (pos != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseUInt(const std::string& value, unsigned int& out) {
    try {
        std::size_t pos = 0;
        unsigned long parsed = std::stoul(value, &pos, 10);
        if (pos != value.size()) {
            return false;
        }
        out = static_cast<unsigned int>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseSize(const std::string& value, std::size_t& out) {
    try {
        std::size_t pos = 0;
        unsigned long long parsed = std::stoull(value, &pos, 10);
        if (pos != value.size()) {
            return false;
        }
        out = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

volatile std::sig_atomic_t gStopRequested = 0;
struct sigaction gOldSigintAction;
struct sigaction gOldSigtermAction;

void onSignal(int /*signum*/) {
    gStopRequested = 1;
}

void installSignalHandlers() {
    std::memset(&gOldSigintAction, 0, sizeof(gOldSigintAction));
    std::memset(&gOldSigtermAction, 0, sizeof(gOldSigtermAction));

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);

    sigaction(SIGINT, &action, &gOldSigintAction);
    sigaction(SIGTERM, &action, &gOldSigtermAction);
}

void restoreSignalHandlers() {
    sigaction(SIGINT, &gOldSigintAction, nullptr);
    sigaction(SIGTERM, &gOldSigtermAction, nullptr);
}

std::string formatDuration(const CliOptions& options) {
    if (options.infinite) {
        return "бесконечно";
    }
    return std::to_string(options.durationSec) + " сек";
}

std::vector<std::string> splitCsv(const std::string& source) {
    std::vector<std::string> out;
    std::string token;
    std::istringstream iss(source);
    while (std::getline(iss, token, ',')) {
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

void printUsage(const std::string& exeName, const AlgorithmRegistry& registry) {
    std::cout
        << "Использование:\n"
        << "  sudo " << exeName << " [опции]\n\n"
        << "Режимы:\n"
        << "  --mode file|stream         file: запись WAV, stream: RAW PCM в stdout\n\n"
        << "Основные опции:\n"
        << "  --output <path>            путь WAV-файла (только mode=file)\n"
        << "  --duration <sec>           длительность генерации, сек (по умолчанию 180)\n"
        << "  --infinite                 бесконечная генерация до Ctrl+C\n"
        << "  --sample-rate <hz>         частота дискретизации (по умолчанию 44100)\n"
        << "  --buffer-ms <ms>           буфер вывода для stream, 0=без буфера (по умолчанию 500)\n"
        << "  --max-memory-mb <mb>       лимит считанной памяти процесса (по умолчанию 60)\n"
        << "  --algorithms <a,b,c>       список ID алгоритмов для выбора\n"
        << "  --timing-mode <m>          режим таймингов (uniform|lognormal|powerlaw|auto)\n"
        << "  --timing-log-sigma <v>     sigma для lognormal-таймингов (по умолчанию 0.60)\n"
        << "  --timing-power-alpha <v>   alpha для power-law таймингов (по умолчанию 1.80)\n"
        << "  --timing-auto-chaos <v>    степень хаоса режима auto [0..1] (по умолчанию 0.55)\n"
        << "  --genetic-mutation-rate <v> вероятность мутации при genetic spawn [0..1]\n"
        << "  --genetic-mutation-depth <v> глубина мутации параметров [0..1]\n"
        << "  --genetic-algo-mutation <v> вероятность смены алгоритма при genetic spawn [0..1]\n"
        << "  --switch-mode <mode>       режим переключения сцен (timer|entropy-triggered)\n"
        << "  --mix-mode <mode>          режим микширования (smoothed)\n"
        << "  --entropy-delta-up <v>     порог роста энтропии RAM для switch (по умолчанию 0.015)\n"
        << "  --entropy-delta-down <v>   порог падения энтропии RAM для switch (по умолчанию 0.015)\n"
        << "  --entropy-hysteresis <v>   hysteresis для entropy-switch (по умолчанию 0.004)\n"
        << "  --switch-cooldown <sec>    cooldown между switch-событиями (по умолчанию 2)\n"
        << "  --scene-macro-min <sec>    min длительность macro-сцены (по умолчанию 30)\n"
        << "  --scene-macro-max <sec>    max длительность macro-сцены (по умолчанию 180)\n"
        << "  --scene-micro-min <ms>     min длительность micro-фазы (по умолчанию 300)\n"
        << "  --scene-micro-max <ms>     max длительность micro-фазы (по умолчанию 4000)\n"
        << "  --target-rms <v>           целевой RMS мастера (по умолчанию 9000)\n"
        << "  --limiter-ceiling <v>      ceiling мягкого лимитера (по умолчанию 28000)\n"
        << "  --limiter-max-gain <v>     максимум makeup gain лимитера (по умолчанию 1.8)\n"
        << "  --min-scene-time <sec>     минимальная длительность сцены (по умолчанию 8)\n"
        << "  --crossfade-ms <ms>        длительность crossfade (по умолчанию 140)\n"
        << "  --switch-prob-base <v>     базовая вероятность switch [0..1] (по умолчанию 0.22)\n"
        << "  --switch-prob-energy <v>   вес energy в switch-prob [0..1] (по умолчанию 0.28)\n"
        << "  --switch-prob-novelty <v>  вес novelty в switch-prob [0..1] (по умолчанию 0.36)\n"
        << "  --switch-prob-hyst <v>     hysteresis для switch-prob [0..1] (по умолчанию 0.08)\n"
        << "  --hmm-tabu-window <n>      tabu-окно для повторов алгоритмов (по умолчанию 3)\n"
        << "  --hmm-novelty-bias <v>     bias к редким алгоритмам [0..1] (по умолчанию 0.22)\n"
        << "  --novelty-threshold <v>    threshold similarity для novelty-guard [0..1]\n"
        << "  --novelty-history <n>      размер окна fingerprint history\n"
        << "  --novelty-cooldown <sec>   cooldown recovery novelty-guard\n"
        << "  --novelty-spawn-extra <n>  доп. голоса при срабатывании novelty-guard\n"
        << "  --band-low-hz <v>          нижняя частота раздела band-split (по умолчанию 220)\n"
        << "  --band-high-hz <v>         верхняя частота раздела band-split (по умолчанию 2600)\n"
        << "  --band-drift-hz <v>        амплитуда дрейфа частот раздела (по умолчанию 90)\n"
        << "  --band-pin-families        закреплять семейства голосов за полосами\n"
        << "  --list-algorithms          показать доступные алгоритмы\n"
        << "  --seed <num>               фиксировать seed (0 = случайный)\n"
        << "  --quiet                    отключить лог прогресса\n"
        << "  --help                     показать эту справку\n\n"
        << "Полифония и тайминги:\n"
        << "  --min-voices <n>           минимум голосов\n"
        << "  --max-voices <n>           максимум голосов\n"
        << "  --memory-switch-min <sec>  минимум между сменами процесса\n"
        << "  --memory-switch-max <sec>  максимум между сменами процесса\n"
        << "  --voice-spawn-min <sec>    минимум между появлением голосов\n"
        << "  --voice-spawn-max <sec>    максимум между появлением голосов\n\n"
        << "Доступные алгоритмы:\n";

    for (const auto& entry : registry.entries()) {
        std::cout << "  - " << entry.id << " : " << entry.description << "\n";
    }
}

bool parseCli(int argc,
              char** argv,
              const AlgorithmRegistry& registry,
              CliOptions& options,
              std::string& error,
              bool& showHelp) {
    showHelp = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto requireValue = [&](const std::string& flag, std::string& out) -> bool {
            if (i + 1 >= argc) {
                error = "Не указано значение для " + flag;
                return false;
            }
            out = argv[++i];
            return true;
        };

        std::string value;

        if (isFlag(arg, "--help", "-h")) {
            showHelp = true;
            return true;
        } else if (isFlag(arg, "--list-algorithms")) {
            options.listAlgorithms = true;
        } else if (isFlag(arg, "--quiet", "-q")) {
            options.verbose = false;
        } else if (isFlag(arg, "--mode", "-m")) {
            if (!requireValue(arg, value)) {
                return false;
            }
            if (value == "file") {
                options.mode = OutputMode::File;
            } else if (value == "stream") {
                options.mode = OutputMode::Stream;
            } else {
                error = "Некорректный mode: " + value + " (ожидается file или stream)";
                return false;
            }
        } else if (isFlag(arg, "--output", "-o")) {
            if (!requireValue(arg, value)) {
                return false;
            }
            options.outputPath = value;
        } else if (isFlag(arg, "--duration", "-d")) {
            if (!requireValue(arg, value) || !parseInt(value, options.durationSec)) {
                error = "Некорректное значение --duration";
                return false;
            }
        } else if (isFlag(arg, "--infinite")) {
            options.infinite = true;
        } else if (isFlag(arg, "--sample-rate", "-r")) {
            if (!requireValue(arg, value) || !parseInt(value, options.sampleRate)) {
                error = "Некорректное значение --sample-rate";
                return false;
            }
        } else if (isFlag(arg, "--buffer-ms")) {
            if (!requireValue(arg, value) || !parseSize(value, options.bufferMs)) {
                error = "Некорректное значение --buffer-ms";
                return false;
            }
        } else if (isFlag(arg, "--max-memory-mb")) {
            if (!requireValue(arg, value) || !parseSize(value, options.maxMemoryMb)) {
                error = "Некорректное значение --max-memory-mb";
                return false;
            }
        } else if (isFlag(arg, "--seed")) {
            if (!requireValue(arg, value) || !parseUInt(value, options.seed)) {
                error = "Некорректное значение --seed";
                return false;
            }
        } else if (isFlag(arg, "--algorithms")) {
            if (!requireValue(arg, value)) {
                return false;
            }
            options.algorithms = splitCsv(value);
        } else if (isFlag(arg, "--timing-mode")) {
            if (!requireValue(arg, value)) {
                return false;
            }
            options.timingMode = value;
        } else if (isFlag(arg, "--timing-log-sigma")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.timingLogSigma)) {
                error = "Некорректное значение --timing-log-sigma";
                return false;
            }
        } else if (isFlag(arg, "--timing-power-alpha")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.timingPowerAlpha)) {
                error = "Некорректное значение --timing-power-alpha";
                return false;
            }
        } else if (isFlag(arg, "--timing-auto-chaos")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.timingAutoChaos)) {
                error = "Некорректное значение --timing-auto-chaos";
                return false;
            }
        } else if (isFlag(arg, "--genetic-mutation-rate")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.geneticMutationRate)) {
                error = "Некорректное значение --genetic-mutation-rate";
                return false;
            }
        } else if (isFlag(arg, "--genetic-mutation-depth")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.geneticMutationDepth)) {
                error = "Некорректное значение --genetic-mutation-depth";
                return false;
            }
        } else if (isFlag(arg, "--genetic-algo-mutation")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.geneticAlgorithmMutation)) {
                error = "Некорректное значение --genetic-algo-mutation";
                return false;
            }
        } else if (isFlag(arg, "--switch-mode")) {
            if (!requireValue(arg, value)) {
                return false;
            }
            options.switchMode = value;
        } else if (isFlag(arg, "--mix-mode")) {
            if (!requireValue(arg, value)) {
                return false;
            }
            options.mixMode = value;
        } else if (isFlag(arg, "--entropy-delta-up")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.entropyDeltaUp)) {
                error = "Некорректное значение --entropy-delta-up";
                return false;
            }
        } else if (isFlag(arg, "--entropy-delta-down")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.entropyDeltaDown)) {
                error = "Некорректное значение --entropy-delta-down";
                return false;
            }
        } else if (isFlag(arg, "--entropy-hysteresis")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.entropyHysteresis)) {
                error = "Некорректное значение --entropy-hysteresis";
                return false;
            }
        } else if (isFlag(arg, "--switch-cooldown")) {
            if (!requireValue(arg, value) || !parseInt(value, options.switchCooldownSec)) {
                error = "Некорректное значение --switch-cooldown";
                return false;
            }
        } else if (isFlag(arg, "--scene-macro-min")) {
            if (!requireValue(arg, value) || !parseInt(value, options.sceneMacroMinSec)) {
                error = "Некорректное значение --scene-macro-min";
                return false;
            }
        } else if (isFlag(arg, "--scene-macro-max")) {
            if (!requireValue(arg, value) || !parseInt(value, options.sceneMacroMaxSec)) {
                error = "Некорректное значение --scene-macro-max";
                return false;
            }
        } else if (isFlag(arg, "--scene-micro-min")) {
            if (!requireValue(arg, value) || !parseInt(value, options.sceneMicroMinMs)) {
                error = "Некорректное значение --scene-micro-min";
                return false;
            }
        } else if (isFlag(arg, "--scene-micro-max")) {
            if (!requireValue(arg, value) || !parseInt(value, options.sceneMicroMaxMs)) {
                error = "Некорректное значение --scene-micro-max";
                return false;
            }
        } else if (isFlag(arg, "--target-rms")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.targetRms)) {
                error = "Некорректное значение --target-rms";
                return false;
            }
        } else if (isFlag(arg, "--limiter-ceiling")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.limiterCeiling)) {
                error = "Некорректное значение --limiter-ceiling";
                return false;
            }
        } else if (isFlag(arg, "--limiter-max-gain")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.limiterMaxGain)) {
                error = "Некорректное значение --limiter-max-gain";
                return false;
            }
        } else if (isFlag(arg, "--min-scene-time")) {
            if (!requireValue(arg, value) || !parseInt(value, options.minSceneTimeSec)) {
                error = "Некорректное значение --min-scene-time";
                return false;
            }
        } else if (isFlag(arg, "--crossfade-ms")) {
            if (!requireValue(arg, value) || !parseInt(value, options.crossfadeMs)) {
                error = "Некорректное значение --crossfade-ms";
                return false;
            }
        } else if (isFlag(arg, "--switch-prob-base")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.switchProbBase)) {
                error = "Некорректное значение --switch-prob-base";
                return false;
            }
        } else if (isFlag(arg, "--switch-prob-energy")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.switchProbEnergyWeight)) {
                error = "Некорректное значение --switch-prob-energy";
                return false;
            }
        } else if (isFlag(arg, "--switch-prob-novelty")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.switchProbNoveltyWeight)) {
                error = "Некорректное значение --switch-prob-novelty";
                return false;
            }
        } else if (isFlag(arg, "--switch-prob-hyst")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.switchProbHysteresis)) {
                error = "Некорректное значение --switch-prob-hyst";
                return false;
            }
        } else if (isFlag(arg, "--hmm-tabu-window")) {
            if (!requireValue(arg, value) || !parseInt(value, options.hmmTabuWindow)) {
                error = "Некорректное значение --hmm-tabu-window";
                return false;
            }
        } else if (isFlag(arg, "--hmm-novelty-bias")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.hmmNoveltyBias)) {
                error = "Некорректное значение --hmm-novelty-bias";
                return false;
            }
        } else if (isFlag(arg, "--novelty-threshold")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.noveltyThreshold)) {
                error = "Некорректное значение --novelty-threshold";
                return false;
            }
        } else if (isFlag(arg, "--novelty-history")) {
            if (!requireValue(arg, value) || !parseInt(value, options.noveltyHistory)) {
                error = "Некорректное значение --novelty-history";
                return false;
            }
        } else if (isFlag(arg, "--novelty-cooldown")) {
            if (!requireValue(arg, value) || !parseInt(value, options.noveltyCooldownSec)) {
                error = "Некорректное значение --novelty-cooldown";
                return false;
            }
        } else if (isFlag(arg, "--novelty-spawn-extra")) {
            if (!requireValue(arg, value) || !parseInt(value, options.noveltySpawnExtra)) {
                error = "Некорректное значение --novelty-spawn-extra";
                return false;
            }
        } else if (isFlag(arg, "--band-low-hz")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.bandSplitLowHz)) {
                error = "Некорректное значение --band-low-hz";
                return false;
            }
        } else if (isFlag(arg, "--band-high-hz")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.bandSplitHighHz)) {
                error = "Некорректное значение --band-high-hz";
                return false;
            }
        } else if (isFlag(arg, "--band-drift-hz")) {
            if (!requireValue(arg, value) || !parseDouble(value, options.bandSplitDriftHz)) {
                error = "Некорректное значение --band-drift-hz";
                return false;
            }
        } else if (isFlag(arg, "--band-pin-families")) {
            options.bandPinFamilies = true;
        } else if (isFlag(arg, "--min-voices")) {
            if (!requireValue(arg, value) || !parseInt(value, options.minVoices)) {
                error = "Некорректное значение --min-voices";
                return false;
            }
        } else if (isFlag(arg, "--max-voices")) {
            if (!requireValue(arg, value) || !parseInt(value, options.maxVoices)) {
                error = "Некорректное значение --max-voices";
                return false;
            }
        } else if (isFlag(arg, "--memory-switch-min")) {
            if (!requireValue(arg, value) || !parseInt(value, options.memorySwitchMinSec)) {
                error = "Некорректное значение --memory-switch-min";
                return false;
            }
        } else if (isFlag(arg, "--memory-switch-max")) {
            if (!requireValue(arg, value) || !parseInt(value, options.memorySwitchMaxSec)) {
                error = "Некорректное значение --memory-switch-max";
                return false;
            }
        } else if (isFlag(arg, "--voice-spawn-min")) {
            if (!requireValue(arg, value) || !parseInt(value, options.voiceSpawnMinSec)) {
                error = "Некорректное значение --voice-spawn-min";
                return false;
            }
        } else if (isFlag(arg, "--voice-spawn-max")) {
            if (!requireValue(arg, value) || !parseInt(value, options.voiceSpawnMaxSec)) {
                error = "Некорректное значение --voice-spawn-max";
                return false;
            }
        } else {
            error = "Неизвестный аргумент: " + arg;
            return false;
        }
    }

    if (options.infinite && options.mode == OutputMode::File) {
        error = "--infinite поддерживается только для --mode stream";
        return false;
    }

    if (!options.infinite && options.durationSec <= 0) {
        error = "--duration должен быть больше нуля";
        return false;
    }

    if (options.minVoices <= 0 || options.maxVoices <= 0 || options.minVoices > options.maxVoices) {
        error = "Диапазон голосов должен быть корректным: min <= max и > 0";
        return false;
    }

    if (options.memorySwitchMinSec <= 0 || options.memorySwitchMaxSec <= 0 ||
        options.memorySwitchMinSec > options.memorySwitchMaxSec) {
        error = "Диапазон смены процесса некорректен";
        return false;
    }

    if (options.voiceSpawnMinSec <= 0 || options.voiceSpawnMaxSec <= 0 ||
        options.voiceSpawnMinSec > options.voiceSpawnMaxSec) {
        error = "Диапазон спавна голосов некорректен";
        return false;
    }

    if (!options.algorithms.empty()) {
        for (const auto& id : options.algorithms) {
            if (!registry.has(id)) {
                error = "Неизвестный алгоритм: " + id;
                return false;
            }
        }
    }

    if (options.timingMode != "uniform" &&
        options.timingMode != "lognormal" &&
        options.timingMode != "powerlaw" &&
        options.timingMode != "auto") {
        error = "Некорректный --timing-mode: " + options.timingMode +
                " (поддерживается: uniform|lognormal|powerlaw|auto)";
        return false;
    }

    if (options.timingLogSigma < 0.05 || options.timingLogSigma > 2.5) {
        error = "--timing-log-sigma должен быть в диапазоне [0.05, 2.5]";
        return false;
    }

    if (options.timingPowerAlpha < 1.05 || options.timingPowerAlpha > 3.5) {
        error = "--timing-power-alpha должен быть в диапазоне [1.05, 3.5]";
        return false;
    }

    if (options.timingAutoChaos < 0.0 || options.timingAutoChaos > 1.0) {
        error = "--timing-auto-chaos должен быть в диапазоне [0, 1]";
        return false;
    }

    if (options.geneticMutationRate < 0.0 || options.geneticMutationRate > 1.0) {
        error = "--genetic-mutation-rate должен быть в диапазоне [0, 1]";
        return false;
    }

    if (options.geneticMutationDepth < 0.0 || options.geneticMutationDepth > 1.0) {
        error = "--genetic-mutation-depth должен быть в диапазоне [0, 1]";
        return false;
    }

    if (options.geneticAlgorithmMutation < 0.0 || options.geneticAlgorithmMutation > 1.0) {
        error = "--genetic-algo-mutation должен быть в диапазоне [0, 1]";
        return false;
    }

    if (options.switchMode != "timer" && options.switchMode != "entropy-triggered") {
        error = "Некорректный --switch-mode: " + options.switchMode +
                " (поддерживается: timer|entropy-triggered)";
        return false;
    }

    if (options.mixMode != "smoothed") {
        error = "Некорректный --mix-mode: " + options.mixMode + " (поддерживается: smoothed)";
        return false;
    }

    if (options.entropyDeltaUp <= 0.0 || options.entropyDeltaUp >= 1.0) {
        error = "--entropy-delta-up должен быть в диапазоне (0, 1)";
        return false;
    }

    if (options.entropyDeltaDown <= 0.0 || options.entropyDeltaDown >= 1.0) {
        error = "--entropy-delta-down должен быть в диапазоне (0, 1)";
        return false;
    }

    if (options.entropyHysteresis < 0.0 || options.entropyHysteresis >= 0.25) {
        error = "--entropy-hysteresis должен быть в диапазоне [0, 0.25)";
        return false;
    }

    if (options.switchCooldownSec < 0) {
        error = "--switch-cooldown должен быть >= 0";
        return false;
    }

    if (options.sceneMacroMinSec <= 0 || options.sceneMacroMaxSec <= 0 ||
        options.sceneMacroMinSec > options.sceneMacroMaxSec) {
        error = "Диапазон --scene-macro-min/--scene-macro-max некорректен";
        return false;
    }

    if (options.sceneMicroMinMs <= 0 || options.sceneMicroMaxMs <= 0 ||
        options.sceneMicroMinMs > options.sceneMicroMaxMs) {
        error = "Диапазон --scene-micro-min/--scene-micro-max некорректен";
        return false;
    }

    if (options.targetRms <= 100.0 || options.targetRms > 20000.0) {
        error = "--target-rms должен быть в диапазоне (100, 20000]";
        return false;
    }

    if (options.limiterCeiling <= 1000.0 || options.limiterCeiling > 32767.0) {
        error = "--limiter-ceiling должен быть в диапазоне (1000, 32767]";
        return false;
    }

    if (options.limiterMaxGain < 0.25 || options.limiterMaxGain > 4.0) {
        error = "--limiter-max-gain должен быть в диапазоне [0.25, 4.0]";
        return false;
    }

    if (options.minSceneTimeSec < 0 || options.minSceneTimeSec > 300) {
        error = "--min-scene-time должен быть в диапазоне [0, 300]";
        return false;
    }

    if (options.crossfadeMs < 0 || options.crossfadeMs > 5000) {
        error = "--crossfade-ms должен быть в диапазоне [0, 5000]";
        return false;
    }

    auto inUnitRange = [](double v) {
        return v >= 0.0 && v <= 1.0;
    };

    if (!inUnitRange(options.switchProbBase)) {
        error = "--switch-prob-base должен быть в диапазоне [0, 1]";
        return false;
    }
    if (!inUnitRange(options.switchProbEnergyWeight)) {
        error = "--switch-prob-energy должен быть в диапазоне [0, 1]";
        return false;
    }
    if (!inUnitRange(options.switchProbNoveltyWeight)) {
        error = "--switch-prob-novelty должен быть в диапазоне [0, 1]";
        return false;
    }
    if (!inUnitRange(options.switchProbHysteresis)) {
        error = "--switch-prob-hyst должен быть в диапазоне [0, 1]";
        return false;
    }

    if (options.hmmTabuWindow < 0 || options.hmmTabuWindow > 16) {
        error = "--hmm-tabu-window должен быть в диапазоне [0, 16]";
        return false;
    }

    if (!inUnitRange(options.hmmNoveltyBias)) {
        error = "--hmm-novelty-bias должен быть в диапазоне [0, 1]";
        return false;
    }

    if (!inUnitRange(options.noveltyThreshold)) {
        error = "--novelty-threshold должен быть в диапазоне [0, 1]";
        return false;
    }

    if (options.noveltyHistory < 8 || options.noveltyHistory > 4096) {
        error = "--novelty-history должен быть в диапазоне [8, 4096]";
        return false;
    }

    if (options.noveltyCooldownSec < 0 || options.noveltyCooldownSec > 300) {
        error = "--novelty-cooldown должен быть в диапазоне [0, 300]";
        return false;
    }

    if (options.noveltySpawnExtra < 0 || options.noveltySpawnExtra > 8) {
        error = "--novelty-spawn-extra должен быть в диапазоне [0, 8]";
        return false;
    }

    if (options.bandSplitLowHz < 40.0 || options.bandSplitLowHz > 1200.0) {
        error = "--band-low-hz должен быть в диапазоне [40, 1200]";
        return false;
    }

    if (options.bandSplitHighHz < 800.0 || options.bandSplitHighHz > 12000.0) {
        error = "--band-high-hz должен быть в диапазоне [800, 12000]";
        return false;
    }

    if (options.bandSplitLowHz >= options.bandSplitHighHz) {
        error = "--band-low-hz должен быть меньше --band-high-hz";
        return false;
    }

    if (options.bandSplitDriftHz < 0.0 || options.bandSplitDriftHz > 2000.0) {
        error = "--band-drift-hz должен быть в диапазоне [0, 2000]";
        return false;
    }

    return true;
}

EngineConfig toEngineConfig(const CliOptions& options) {
    EngineConfig config;
    config.durationSec = options.durationSec;
    config.infinite = options.infinite;
    config.sampleRate = options.sampleRate;
    config.maxMemoryBytes = options.maxMemoryMb * 1024ULL * 1024ULL;
    config.minVoices = options.minVoices;
    config.maxVoices = options.maxVoices;
    config.memorySwitchMinSec = options.memorySwitchMinSec;
    config.memorySwitchMaxSec = options.memorySwitchMaxSec;
    config.voiceSpawnMinSec = options.voiceSpawnMinSec;
    config.voiceSpawnMaxSec = options.voiceSpawnMaxSec;
    config.timingMode = options.timingMode;
    config.timingLogSigma = options.timingLogSigma;
    config.timingPowerAlpha = options.timingPowerAlpha;
    config.timingAutoChaos = options.timingAutoChaos;
    config.geneticMutationRate = options.geneticMutationRate;
    config.geneticMutationDepth = options.geneticMutationDepth;
    config.geneticAlgorithmMutation = options.geneticAlgorithmMutation;
    config.switchMode = options.switchMode;
    config.mixMode = options.mixMode;
    config.entropyDeltaUp = options.entropyDeltaUp;
    config.entropyDeltaDown = options.entropyDeltaDown;
    config.entropyHysteresis = options.entropyHysteresis;
    config.switchCooldownSec = options.switchCooldownSec;
    config.sceneMacroMinSec = options.sceneMacroMinSec;
    config.sceneMacroMaxSec = options.sceneMacroMaxSec;
    config.sceneMicroMinMs = options.sceneMicroMinMs;
    config.sceneMicroMaxMs = options.sceneMicroMaxMs;
    config.targetRms = options.targetRms;
    config.limiterCeiling = options.limiterCeiling;
    config.limiterMaxGain = options.limiterMaxGain;
    config.minSceneTimeSec = options.minSceneTimeSec;
    config.crossfadeMs = options.crossfadeMs;
    config.switchProbBase = options.switchProbBase;
    config.switchProbEnergyWeight = options.switchProbEnergyWeight;
    config.switchProbNoveltyWeight = options.switchProbNoveltyWeight;
    config.switchProbHysteresis = options.switchProbHysteresis;
    config.hmmTabuWindow = options.hmmTabuWindow;
    config.hmmNoveltyBias = options.hmmNoveltyBias;
    config.noveltyThreshold = options.noveltyThreshold;
    config.noveltyHistory = options.noveltyHistory;
    config.noveltyCooldownSec = options.noveltyCooldownSec;
    config.noveltySpawnExtra = options.noveltySpawnExtra;
    config.bandSplitLowHz = options.bandSplitLowHz;
    config.bandSplitHighHz = options.bandSplitHighHz;
    config.bandSplitDriftHz = options.bandSplitDriftHz;
    config.bandPinFamilies = options.bandPinFamilies;
    config.seed = options.seed;
    config.verbose = options.verbose;
    config.stopFlag = &gStopRequested;
    config.allowedAlgorithmIds = options.algorithms;
    return config;
}
}  // namespace

int main(int argc, char** argv) {
    AlgorithmRegistry registry = createDefaultAlgorithmRegistry();

    CliOptions options;
    std::string cliError;
    bool showHelp = false;

    if (!parseCli(argc, argv, registry, options, cliError, showHelp)) {
        std::cerr << "Ошибка: " << cliError << "\n\n";
        printUsage(argv[0], registry);
        return 1;
    }

    if (showHelp) {
        printUsage(argv[0], registry);
        return 0;
    }

    if (options.listAlgorithms) {
        std::cout << "Алгоритмы:\n";
        for (const auto& entry : registry.entries()) {
            std::cout << "- " << entry.id << " : " << entry.description << "\n";
        }
        return 0;
    }

    if (::geteuid() != 0) {
        std::cerr << "Ошибка: требуется root для чтения /proc/*/mem\n";
        std::cerr << "Запуск: sudo " << argv[0] << " [опции]\n";
        return 1;
    }

    installSignalHandlers();

    EngineConfig config = toEngineConfig(options);
    RamAudioEngine engine(config, registry);

    RunStats stats;
    std::string runError;

    std::unique_ptr<WavFileSink> wavHolder;
    std::unique_ptr<RawStdoutSink> streamHolder;
    std::unique_ptr<BufferedRawStdoutSink> bufferedStreamHolder;
    OutputSink* sink = nullptr;

    if (options.mode == OutputMode::File) {
        wavHolder = std::make_unique<WavFileSink>(options.outputPath, options.sampleRate);
        if (!wavHolder->good()) {
            std::cerr << "Ошибка: " << wavHolder->error() << "\n";
            return 1;
        }
        sink = wavHolder.get();
    } else {
        if (options.bufferMs == 0) {
            streamHolder = std::make_unique<RawStdoutSink>();
            sink = streamHolder.get();
        } else {
            const std::size_t sampleRate = static_cast<std::size_t>(std::max(1, options.sampleRate));
            const std::size_t bufferedSamples = std::max<std::size_t>(
                1,
                (sampleRate * options.bufferMs + 999U) / 1000U);
            bufferedStreamHolder = std::make_unique<BufferedRawStdoutSink>(bufferedSamples, &gStopRequested);
            if (!bufferedStreamHolder->good()) {
                std::cerr << "Ошибка: " << bufferedStreamHolder->error() << "\n";
                restoreSignalHandlers();
                return 1;
            }
            sink = bufferedStreamHolder.get();
        }
    }

    if (options.verbose) {
        std::cerr << "Генерация RAM-аудио: " << formatDuration(options) << ", "
                  << config.sampleRate << " Hz, режим="
                  << (options.mode == OutputMode::File ? "file" : "stream") << std::endl;
        if (options.mode == OutputMode::Stream) {
            std::cerr << "Буфер stream: " << options.bufferMs << " ms"
                      << (options.bufferMs == 0 ? " (выключен)" : "") << std::endl;
        }
        std::cerr << "Switch mode: " << options.switchMode
                  << ", Mix mode: " << options.mixMode << std::endl;
        if (options.switchMode == "entropy-triggered") {
            std::cerr << "Entropy switch config: up=" << options.entropyDeltaUp
                      << ", down=" << options.entropyDeltaDown
                      << ", hysteresis=" << options.entropyHysteresis
                      << ", cooldown=" << options.switchCooldownSec << "s" << std::endl;
        }
        if (options.infinite) {
            std::cerr << "Остановка: Ctrl+C" << std::endl;
        }
    }

    const bool ok = engine.run(*sink, stats, runError);

    restoreSignalHandlers();

    if (!ok) {
        std::cerr << "Ошибка генерации: " << runError << "\n";
        return 1;
    }

    if (options.mode == OutputMode::File && options.verbose) {
        std::cerr << "Готово: " << options.outputPath << std::endl;
    } else if (options.mode == OutputMode::Stream && options.infinite && options.verbose) {
        std::cerr << "Стрим остановлен пользователем" << std::endl;
    }

    return 0;
}
