#include "SessionLaunchService.hpp"

#include "../engine/AudioEngine.hpp"
#include "TrackInfo.hpp"
#include "TrackManager.hpp"

namespace magda::SessionLaunchService {

void launchScene(const std::vector<TrackId>& trackIds, int sceneIndex) {
    // The engine's, because a scene is one event and only the engine can make
    // it one: launching slot by slot from here puts them on two sides of a
    // boundary the loop straddled (#2552).
    if (auto* engine = TrackManager::getInstance().getAudioEngine(); engine != nullptr)
        engine->launchSessionScene(trackIds, sceneIndex);
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
