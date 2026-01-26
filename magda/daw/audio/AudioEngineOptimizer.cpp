#include "AudioEngineOptimizer.hpp"

#include <juce_core/juce_core.h>

namespace magda {

AudioEngineOptimizer::AudioEngineOptimizer() {
    ViewModeController::getInstance().addListener(this);
}

AudioEngineOptimizer::~AudioEngineOptimizer() {
    ViewModeController::getInstance().removeListener(this);
}

void AudioEngineOptimizer::viewModeChanged(const ViewMode mode, const AudioEngineProfile& profile) {
    DBG("View mode changed to: " << getViewModeName(mode));
    applyProfile(profile);
}

void AudioEngineOptimizer::applyProfile(const AudioEngineProfile& profile) {
    // TODO: Apply profile to TracktionEngineWrapper
    DBG("Applying audio profile - Buffer: "
        << profile.bufferSize << " samples, Latency: " << profile.latencyMs
        << "ms, Low latency: " << (profile.lowLatencyMode ? "yes" : "no")
        << ", Multi-threaded: " << (profile.multiThreaded ? "yes" : "no"));
}

}  // namespace magda
