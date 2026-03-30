#include "audio_io.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

void writeLE16(std::ostream& stream, std::uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    stream.write(bytes, 2);
}

void writeLE32(std::ostream& stream, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    stream.write(bytes, 4);
}

}  // namespace

WavFileSink::WavFileSink(const std::string& outputPath, int sampleRate)
    : outputPath_(outputPath),
      sampleRate_(sampleRate),
      sampleCount_(0) {
    stream_.open(outputPath_, std::ios::binary | std::ios::trunc);
    if (!stream_.is_open()) {
        error_ = "Не удалось открыть WAV файл для записи";
        return;
    }

    if (!writeHeader(0U)) {
        if (error_.empty()) {
            error_ = "Не удалось записать WAV заголовок";
        }
    }
}

WavFileSink::~WavFileSink() {
    if (stream_.is_open()) {
        finalize();
    }
}

bool WavFileSink::good() const {
    return error_.empty() && stream_.good();
}

const std::string& WavFileSink::error() const {
    return error_;
}

bool WavFileSink::writeSample(std::int16_t sample) {
    if (!good()) {
        return false;
    }

    writeLE16(stream_, static_cast<std::uint16_t>(sample));
    if (!stream_.good()) {
        error_ = "Ошибка записи WAV данных";
        return false;
    }

    ++sampleCount_;
    return true;
}

bool WavFileSink::finalize() {
    if (!stream_.is_open()) {
        return false;
    }

    if (!error_.empty()) {
        stream_.close();
        return false;
    }

    const std::uint64_t dataSize64 = static_cast<std::uint64_t>(sampleCount_) * 2ULL;
    if (dataSize64 > 0xFFFFFFFFULL) {
        error_ = "WAV файл превышает лимит 4 GB";
        stream_.close();
        return false;
    }

    const std::uint32_t dataSize = static_cast<std::uint32_t>(dataSize64);

    stream_.seekp(0, std::ios::beg);
    if (!writeHeader(dataSize)) {
        if (error_.empty()) {
            error_ = "Не удалось обновить WAV заголовок";
        }
        stream_.close();
        return false;
    }

    stream_.close();
    return error_.empty();
}

bool WavFileSink::writeHeader(std::uint32_t dataSize) {
    if (!stream_.good()) {
        error_ = "Поток WAV недоступен";
        return false;
    }

    const std::uint16_t channels = 1;
    const std::uint16_t bitsPerSample = 16;
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8U));
    const std::uint32_t byteRate = static_cast<std::uint32_t>(sampleRate_) * blockAlign;
    const std::uint32_t riffSize = 36U + dataSize;

    stream_.write("RIFF", 4);
    writeLE32(stream_, riffSize);
    stream_.write("WAVE", 4);
    stream_.write("fmt ", 4);
    writeLE32(stream_, 16U);
    writeLE16(stream_, 1U);
    writeLE16(stream_, channels);
    writeLE32(stream_, static_cast<std::uint32_t>(sampleRate_));
    writeLE32(stream_, byteRate);
    writeLE16(stream_, blockAlign);
    writeLE16(stream_, bitsPerSample);
    stream_.write("data", 4);
    writeLE32(stream_, dataSize);

    if (!stream_.good()) {
        error_ = "Ошибка записи WAV заголовка";
        return false;
    }

    return true;
}

bool RawStdoutSink::writeSample(std::int16_t sample) {
    std::cout.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
    return std::cout.good();
}

bool RawStdoutSink::finalize() {
    std::cout.flush();
    return std::cout.good();
}

BufferedRawStdoutSink::BufferedRawStdoutSink(std::size_t maxSamples,
                                             const volatile std::sig_atomic_t* stopFlag)
    : maxSamples_(std::max<std::size_t>(1, maxSamples)),
      stopFlag_(stopFlag),
      finishing_(false),
      finalized_(false),
      writeError_(false) {
    worker_ = std::thread(&BufferedRawStdoutSink::workerLoop, this);
}

BufferedRawStdoutSink::~BufferedRawStdoutSink() {
    finalize();
}

bool BufferedRawStdoutSink::good() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !writeError_;
}

std::string BufferedRawStdoutSink::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

bool BufferedRawStdoutSink::writeSample(std::int16_t sample) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (finalized_ || writeError_) {
        return false;
    }

    while (queue_.size() >= maxSamples_ && !finishing_ && !writeError_) {
        if (stopFlag_ != nullptr && *stopFlag_ != 0) {
            return false;
        }
        canWriteCv_.wait_for(lock, std::chrono::milliseconds(20));
    }

    if (stopFlag_ != nullptr && *stopFlag_ != 0) {
        return false;
    }

    if (finishing_ || writeError_ || finalized_) {
        return false;
    }

    queue_.push_back(sample);
    canReadCv_.notify_one();
    return true;
}

bool BufferedRawStdoutSink::finalize() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (finalized_) {
            return !writeError_;
        }
        finishing_ = true;
    }

    canReadCv_.notify_all();
    canWriteCv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        finalized_ = true;
        return !writeError_;
    }
}

void BufferedRawStdoutSink::workerLoop() {
    while (true) {
        std::int16_t sample = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            canReadCv_.wait(lock, [&]() {
                return !queue_.empty() || finishing_;
            });

            if (queue_.empty()) {
                break;
            }

            sample = queue_.front();
            queue_.pop_front();
            canWriteCv_.notify_one();
        }

        std::cout.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
        if (!std::cout.good()) {
            if (stopFlag_ != nullptr && *stopFlag_ != 0) {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            writeError_ = true;
            finishing_ = true;
            if (error_.empty()) {
                error_ = "Ошибка записи в stdout";
            }
            canWriteCv_.notify_all();
            canReadCv_.notify_all();
            return;
        }
    }

    std::cout.flush();
    if (!std::cout.good()) {
        if (stopFlag_ != nullptr && *stopFlag_ != 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        writeError_ = true;
        if (error_.empty()) {
            error_ = "Ошибка flush stdout";
        }
    }
}
