#include "TracktionEngineWrapper.hpp"

#include "../audio/SessionClipScheduler.hpp"

namespace magda {

// =============================================================================
// PDC Query Methods
// =============================================================================

double TracktionEngineWrapper::getPluginLatencySeconds(const std::string& effect_id) const {
    // TODO: Implement when we have effect tracking
    // For now, iterate all tracks and their plugins to find by ID
    juce::ignoreUnused(effect_id);
    return 0.0;
}

double TracktionEngineWrapper::getGlobalLatencySeconds() const {
    if (!currentEdit_) {
        return 0.0;
    }

    // Get the playback context which contains the audio graph
    auto* context = currentEdit_->getCurrentPlaybackContext();
    if (!context) {
        return 0.0;
    }

    // Tracktion Engine calculates PDC automatically and stores max latency
    // The easiest approach is to iterate all tracks and find max plugin latency
    double maxLatency = 0.0;

    for (auto* track : currentEdit_->getTrackList()) {
        if (auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Check all plugins on this track
            for (auto* plugin : audioTrack->pluginList) {
                // Use base Plugin class method (works for all plugin types)
                maxLatency = std::max(maxLatency, plugin->getLatencySeconds());
            }
        }
    }

    // Add device latency
    maxLatency += engine_->getDeviceManager().getOutputLatencySeconds();

    return maxLatency;
}

double TracktionEngineWrapper::getSessionPlayheadPosition() const {
    if (sessionScheduler_)
        return sessionScheduler_->getSessionPlayheadPosition();
    return -1.0;
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
