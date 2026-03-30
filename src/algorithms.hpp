#ifndef RAM_AUDIO_ALGORITHMS_HPP
#define RAM_AUDIO_ALGORITHMS_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class IRamAlgorithm {
public:
    virtual ~IRamAlgorithm() = default;

    virtual const std::string& id() const = 0;
    virtual double generate(std::uint64_t sampleIndex,
                            const std::vector<std::uint8_t>& memory,
                            double macroMod) = 0;
    virtual void onMemorySizeChanged(std::size_t newMemorySize) = 0;
    virtual bool prefersHighResolution() const { return false; }
};

using AlgorithmFactory = std::function<std::unique_ptr<IRamAlgorithm>(
    std::size_t memorySize,
    int sampleRate,
    std::mt19937& rng)>;

struct AlgorithmEntry {
    std::string id;
    std::string description;
    AlgorithmFactory factory;
};

class AlgorithmRegistry {
public:
    bool registerAlgorithm(AlgorithmEntry entry);

    bool has(const std::string& id) const;
    const AlgorithmEntry* get(const std::string& id) const;

    std::vector<AlgorithmEntry> entries() const;
    std::vector<std::string> ids() const;

    std::unique_ptr<IRamAlgorithm> create(const std::string& id,
                                          std::size_t memorySize,
                                          int sampleRate,
                                          std::mt19937& rng) const;

    std::unique_ptr<IRamAlgorithm> createRandom(const std::vector<std::string>& allowedIds,
                                                std::size_t memorySize,
                                                int sampleRate,
                                                std::mt19937& rng) const;

private:
    std::vector<AlgorithmEntry> orderedEntries_;
    std::unordered_map<std::string, std::size_t> indexById_;
};

AlgorithmRegistry createDefaultAlgorithmRegistry();

#endif
