#include "telemetry.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;

bool expectRange(const std::string& name, double value, double minValue, double maxValue) {
    if (value < minValue || value > maxValue) {
        std::cerr << "[FAIL] " << name << " out of range: " << value
                  << " expected in [" << minValue << ", " << maxValue << "]\n";
        return false;
    }
    return true;
}

bool expectTrue(const std::string& name, bool value) {
    if (!value) {
        std::cerr << "[FAIL] " << name << " expected true\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool ok = true;

    {
        AudioTelemetry telemetry(44100, 2048, 256);
        for (std::uint64_t i = 0; i < 4096; ++i) {
            const double sample = 0.8 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / 44100.0);
            telemetry.pushSample(sample, i);
        }

        const TelemetryMetrics m = telemetry.metrics();
        ok = expectTrue("sine metrics valid", m.valid) && ok;
        ok = expectRange("sine rms", m.rms, 0.0, 1.0) && ok;
        ok = expectRange("sine centroid", m.spectralCentroidHz, 0.0, 22050.0) && ok;
        ok = expectRange("sine flatness", m.spectralFlatness, 0.0, 1.0) && ok;
        ok = expectRange("sine zcr", m.zeroCrossingRate, 0.0, 1.0) && ok;
        ok = expectRange("sine transientDensity", m.transientDensity, 0.0, 1.0) && ok;

        if (m.windowSize != 2048) {
            std::cerr << "[FAIL] window size mismatch: " << m.windowSize << " expected 2048\n";
            ok = false;
        }
    }

    {
        AudioTelemetry telemetry(44100, 2048, 128);
        for (std::uint64_t i = 0; i < 4096; ++i) {
            double sample = 0.0;
            if (i % 64 == 0) {
                sample = 1.0;
            }
            telemetry.pushSample(sample, i);
        }

        const TelemetryMetrics m = telemetry.metrics();
        ok = expectTrue("impulse metrics valid", m.valid) && ok;
        ok = expectRange("impulse rms", m.rms, 0.0, 1.0) && ok;
        ok = expectRange("impulse centroid", m.spectralCentroidHz, 0.0, 22050.0) && ok;
        ok = expectRange("impulse flatness", m.spectralFlatness, 0.0, 1.0) && ok;
        ok = expectRange("impulse zcr", m.zeroCrossingRate, 0.0, 1.0) && ok;
        ok = expectRange("impulse transientDensity", m.transientDensity, 0.0, 1.0) && ok;
    }

    if (!ok) {
        return 1;
    }

    std::cout << "telemetry_test: OK\n";
    return 0;
}
