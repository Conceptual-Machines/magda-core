#include "SessionLaunchService.hpp"

#include "../engine/AudioEngine.hpp"
#include "ClipManager.hpp"
#include "TrackInfo.hpp"
#include "TrackManager.hpp"

namespace magda::SessionLaunchService {

void launchScene(const std::vector<TrackId>& trackIds, int sceneIndex) {
    auto& cm = ClipManager::getInstance();
    auto* engine = TrackManager::getInstance().getAudioEngine();
    for (auto trackId : trackIds) {
        ClipId clipId = cm.getClipInSlot(trackId, sceneIndex);
        if (clipId != INVALID_CLIP_ID) {
            cm.triggerClip(clipId);
        } else if (engine) {
            engine->stopSessionTrack(trackId);
        }
    }
}

void launchSceneAllTracks(int sceneIndex) {
    const auto& tracks = TrackManager::getInstance().getTracks();
    std::vector<TrackId> ids;
    ids.reserve(tracks.size());
    for (const auto& t : tracks)
        ids.push_back(t.id);
    launchScene(ids, sceneIndex);
}

}  // namespace magda::SessionLaunchService
