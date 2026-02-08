#include "TracktionEngineWrapper.hpp"

namespace magda {

// =============================================================================
// Constructor and Destructor
// =============================================================================

TracktionEngineWrapper::TracktionEngineWrapper() = default;

TracktionEngineWrapper::~TracktionEngineWrapper() {
    shutdown();
}

// =============================================================================
// Helper Methods
// =============================================================================

tracktion::Track* TracktionEngineWrapper::findTrackById(const std::string& track_id) const {
    auto it = trackMap_.find(track_id);
    return (it != trackMap_.end()) ? it->second.get() : nullptr;
}

tracktion::Clip* TracktionEngineWrapper::findClipById(const std::string& clip_id) const {
    auto it = clipMap_.find(clip_id);
    return (it != clipMap_.end()) ? static_cast<tracktion::Clip*>(it->second.get()) : nullptr;
}

std::string TracktionEngineWrapper::generateTrackId() {
    return "track_" + std::to_string(nextTrackId_++);
}

std::string TracktionEngineWrapper::generateClipId() {
    return "clip_" + std::to_string(nextClipId_++);
}

std::string TracktionEngineWrapper::generateEffectId() {
    return "effect_" + std::to_string(nextEffectId_++);
}

}  // namespace magda
