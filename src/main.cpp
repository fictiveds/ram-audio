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
