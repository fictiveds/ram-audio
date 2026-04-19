#ifndef RAM_AUDIO_AUDIO_IO_HPP
#define RAM_AUDIO_AUDIO_IO_HPP

#include "ram_audio_engine.hpp"

#include <csignal>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class WavFileSink final : public OutputSink {
public:
    WavFileSink(const std::string& outputPath, int sampleRate, UiLanguage language = UiLanguage::English);
    ~WavFileSink() override;

    bool good() const;
    const std::string& error() const;

    bool writeSample(std::int16_t sample) override;
    bool finalize() override;

private:
    bool writeHeader(std::uint32_t dataSize);

    std::string outputPath_;
    int sampleRate_;
    UiLanguage language_;
    std::ofstream stream_;
    std::size_t sampleCount_;
    std::string error_;
};

class RawStdoutSink final : public OutputSink {
public:
    bool writeSample(std::int16_t sample) override;
    bool finalize() override;
};

class BufferedRawStdoutSink final : public OutputSink {
public:
    BufferedRawStdoutSink(std::size_t maxSamples,
                          const volatile std::sig_atomic_t* stopFlag,
                          UiLanguage language = UiLanguage::English);
    ~BufferedRawStdoutSink() override;

    bool good() const;
    std::string error() const;

    bool writeSample(std::int16_t sample) override;
    bool finalize() override;

private:
    void workerLoop();

    std::size_t maxSamples_;
    const volatile std::sig_atomic_t* stopFlag_;
    UiLanguage language_;

    mutable std::mutex mutex_;
    std::condition_variable canReadCv_;
    std::condition_variable canWriteCv_;
    std::deque<std::int16_t> queue_;
    std::thread worker_;

    bool finishing_;
    bool finalized_;
    bool writeError_;
    std::string error_;
};

#endif
