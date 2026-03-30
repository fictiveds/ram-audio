#ifndef RAM_AUDIO_ALGORITHMS_COMMON_HPP
#define RAM_AUDIO_ALGORITHMS_COMMON_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace ram_audio {
namespace algo_detail {

constexpr double kPi = 3.14159265358979323846;

inline int randomIntInclusive(std::mt19937& rng, int minVal, int maxVal) {
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(rng);
}

inline double randomDouble(std::mt19937& rng, double minVal, double maxVal) {
    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return dist(rng);
}

inline std::size_t boundedIndex(std::size_t idx, std::size_t memorySize) {
    if (memorySize == 0) {
        return 0;
    }
    return idx % memorySize;
}

inline std::int16_t readInt16LE(const std::vector<std::uint8_t>& memory, std::size_t idx) {
    if (memory.size() < 2) {
        return 0;
    }
    const std::size_t i0 = idx % (memory.size() - 1);
    const std::uint16_t lo = memory[i0];
    const std::uint16_t hi = memory[i0 + 1];
    return static_cast<std::int16_t>((hi << 8U) | lo);
}

}  // namespace algo_detail
}  // namespace ram_audio

#endif
