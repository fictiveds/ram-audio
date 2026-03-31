#ifndef RAM_AUDIO_ALGORITHM_REGISTRY_HPP
#define RAM_AUDIO_ALGORITHM_REGISTRY_HPP

#include "../algorithms.hpp"

namespace ram_audio {

void registerClassicAlgorithms(AlgorithmRegistry& registry);
void registerTextureAlgorithms(AlgorithmRegistry& registry);
void registerAdvancedAlgorithms(AlgorithmRegistry& registry);
void registerMusicalAlgorithms(AlgorithmRegistry& registry);
void registerExperimentalAlgorithms(AlgorithmRegistry& registry);

}  // namespace ram_audio

#endif
