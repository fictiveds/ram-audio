#ifndef RAM_AUDIO_TELEMETRY_HPP
#define RAM_AUDIO_TELEMETRY_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

struct TelemetryMetrics {
    double rms = 0.0;
    double spectralCentroidHz = 0.0;
    double spectralFlatness = 0.0;
    double zeroCrossingRate = 0.0;
    double transientDensity = 0.0;
    std::size_t windowSize = 0;
    std::uint64_t sampleIndex = 0;
    bool valid = false;
};

class AudioTelemetry {
public:
    AudioTelemetry(int sampleRate, std::size_t windowSize = 4096, std::size_t hopSize = 512);

    void pushSample(double sample, std::uint64_t sampleIndex);
    const TelemetryMetrics& metrics() const;

private:
    void computeMetrics(std::uint64_t sampleIndex);
    std::vector<double> copyWindow() const;

    int sampleRate_;
    std::size_t windowSize_;
    std::size_t hopSize_;
    std::vector<double> ring_;
    std::size_t writePos_;
    std::size_t filled_;
    std::size_t samplesSinceUpdate_;
    TelemetryMetrics metrics_;
};

#endif
