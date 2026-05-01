#include "session_api_live.hpp"

#include "../core/ClipManager.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../engine/AudioEngine.hpp"

namespace magda {

void SessionApiLive::launchClip(ClipId clipId) {
    ClipManager::getInstance().triggerClip(clipId);
}

void SessionApiLive::stopClip(ClipId clipId) {
    ClipManager::getInstance().stopClip(clipId);
}

void SessionApiLive::stopTrack(TrackId trackId) {
    auto activeId = getActiveClipOnTrack(trackId);
    if (activeId != INVALID_CLIP_ID) {
        ClipManager::getInstance().stopClip(activeId);
    }
}

void SessionApiLive::stopAll() {
    ClipManager::getInstance().stopAllClips();
}

void SessionApiLive::launchScene(int sceneIndex) {
    // Mirror SessionView::onSceneLaunched: trigger every clip in this scene,
    // and stop the active clip on tracks whose slot is empty so the row
    // collapses to "play these, stop the rest" — same semantics whether the
    // user clicked the scene button or a script called launchScene.
    auto& cm = ClipManager::getInstance();
    auto* engine = TrackManager::getInstance().getAudioEngine();
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        ClipId clipId = cm.getClipInSlot(track.id, sceneIndex);
        if (clipId != INVALID_CLIP_ID) {
            cm.triggerClip(clipId);
        } else if (engine) {
            engine->stopSessionTrack(track.id);
        }
    }
}

ClipId SessionApiLive::getActiveClipOnTrack(TrackId trackId) const {
    auto* track = TrackManager::getInstance().getTrack(trackId);
    return track != nullptr ? track->activeSessionClipId : INVALID_CLIP_ID;
}

ClipId SessionApiLive::getClipInSlot(TrackId trackId, int sceneIndex) const {
    return ClipManager::getInstance().getClipInSlot(trackId, sceneIndex);
}

SessionClipPlayState SessionApiLive::getClipPlayState(ClipId clipId) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr || clipId == INVALID_CLIP_ID)
        return SessionClipPlayState::Stopped;
    return engine->getSessionClipPlayState(clipId);
}

}  // namespace magda
