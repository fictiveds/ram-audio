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
    void copyWindow(std::vector<double>& out) const;
    void initSpectralTables();

    int sampleRate_;
    std::size_t windowSize_;
    std::size_t hopSize_;
    std::size_t spectralSize_;
    std::size_t spectralBins_;
    std::size_t spectralStride_;
    std::vector<double> ring_;
    std::vector<double> orderedWindow_;
    std::vector<double> spectralWindow_;
    std::vector<double> hannWindow_;
    std::vector<double> cosTable_;
    std::vector<double> sinTable_;
    std::vector<double> binFreqsHz_;
    std::vector<double> diffsScratch_;
    std::size_t spectralUpdateStride_;
    std::size_t spectralUpdateCounter_;
    std::size_t writePos_;
    std::size_t filled_;
    std::size_t samplesSinceUpdate_;
    TelemetryMetrics metrics_;
};

#endif
