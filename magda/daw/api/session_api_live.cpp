#include "session_api_live.hpp"

#include "../core/ClipManager.hpp"
#include "../core/SessionLaunchService.hpp"
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
    SessionLaunchService::launchSceneAllTracks(sceneIndex);
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
