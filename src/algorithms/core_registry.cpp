#include "../algorithms.hpp"
#include "algorithm_registry.hpp"

#include <random>
#include <utility>

bool AlgorithmRegistry::registerAlgorithm(AlgorithmEntry entry) {
    if (entry.id.empty() || !entry.factory) {
        return false;
    }
    if (indexById_.find(entry.id) != indexById_.end()) {
        return false;
    }
    indexById_[entry.id] = orderedEntries_.size();
    orderedEntries_.push_back(std::move(entry));
    return true;
}

bool AlgorithmRegistry::has(const std::string& id) const {
    return indexById_.find(id) != indexById_.end();
}

const AlgorithmEntry* AlgorithmRegistry::get(const std::string& id) const {
    const auto it = indexById_.find(id);
    if (it == indexById_.end()) {
        return nullptr;
    }
    return &orderedEntries_[it->second];
}

std::vector<AlgorithmEntry> AlgorithmRegistry::entries() const {
    return orderedEntries_;
}

std::vector<std::string> AlgorithmRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(orderedEntries_.size());
    for (const auto& e : orderedEntries_) {
        out.push_back(e.id);
    }
    return out;
}

std::unique_ptr<IRamAlgorithm> AlgorithmRegistry::create(const std::string& id,
                                                          std::size_t memorySize,
                                                          int sampleRate,
                                                          std::mt19937& rng) const {
    const AlgorithmEntry* entry = get(id);
    if (entry == nullptr) {
        return nullptr;
    }
    return entry->factory(memorySize, sampleRate, rng);
}

std::unique_ptr<IRamAlgorithm> AlgorithmRegistry::createRandom(const std::vector<std::string>& allowedIds,
                                                                std::size_t memorySize,
                                                                int sampleRate,
                                                                std::mt19937& rng) const {
    std::vector<const AlgorithmEntry*> candidates;
    candidates.reserve(orderedEntries_.size());

    if (allowedIds.empty()) {
        for (const auto& entry : orderedEntries_) {
            candidates.push_back(&entry);
        }
    } else {
        for (const auto& id : allowedIds) {
            const AlgorithmEntry* entry = get(id);
            if (entry != nullptr) {
                candidates.push_back(entry);
            }
        }
    }

    if (candidates.empty()) {
        return nullptr;
    }

    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    const AlgorithmEntry* selected = candidates[dist(rng)];
    return selected->factory(memorySize, sampleRate, rng);
}

AlgorithmRegistry createDefaultAlgorithmRegistry() {
    AlgorithmRegistry registry;

    ram_audio::registerClassicAlgorithms(registry);
    ram_audio::registerTextureAlgorithms(registry);
    ram_audio::registerAdvancedAlgorithms(registry);
    ram_audio::registerMusicalAlgorithms(registry);
    ram_audio::registerExperimentalAlgorithms(registry);

    return registry;
}
