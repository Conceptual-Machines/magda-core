#include "session_api_live.hpp"

#include <algorithm>
#include <unordered_set>

#include "../core/ClipManager.hpp"
#include "../core/SessionLaunchService.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../engine/AudioEngine.hpp"
#include "../project/ProjectManager.hpp"

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

bool SessionApiLive::isSlotRecordArmed(TrackId trackId, int sceneIndex) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    return engine != nullptr && engine->isSessionSlotRecordArmed(trackId, sceneIndex);
}

bool SessionApiLive::isSlotRecording(TrackId trackId, int sceneIndex) const {
    auto* engine = TrackManager::getInstance().getAudioEngine();
    return engine != nullptr && engine->isSessionSlotRecording(trackId, sceneIndex);
}

bool SessionApiLive::setClipLaunchSettings(ClipId clipId,
                                           const SessionClipLaunchSettings& settings) {
    auto& clips = ClipManager::getInstance();
    auto* clip = clips.getClip(clipId);
    if (clip == nullptr || clip->view != ClipView::Session)
        return false;
    const SessionClipLaunchSettings current{clip->launchMode, clip->launchQuantize,
                                            clip->followAction, clip->followActionDelayBeats,
                                            clip->followActionLoopCount};
    if (current == settings)
        return false;

    clip->launchMode = settings.launchMode;
    clip->launchQuantize = settings.launchQuantize;
    clip->followAction = settings.followAction;
    clip->followActionDelayBeats = settings.followActionDelayBeats;
    clip->followActionLoopCount = settings.followActionLoopCount;
    clips.forceNotifyClipPropertyChanged(clipId);
    return true;
}

bool SessionApiLive::returnToArrangement(std::optional<TrackId> trackId) {
    auto& tracks = TrackManager::getInstance();
    auto* engine = tracks.getAudioEngine();

    if (trackId) {
        const auto* track = tracks.getTrack(*trackId);
        if (track == nullptr)
            return false;
        const bool changed = track->playbackMode == TrackPlaybackMode::Session ||
                             track->activeSessionClipId != INVALID_CLIP_ID;
        if (!changed)
            return false;
        if (engine != nullptr)
            engine->stopSessionTrack(*trackId);
        else
            tracks.setTrackPlaybackMode(*trackId, TrackPlaybackMode::Arrangement);
        return true;
    }

    const bool changed = std::ranges::any_of(tracks.getTracks(), [](const TrackInfo& track) {
        return track.playbackMode == TrackPlaybackMode::Session ||
               track.activeSessionClipId != INVALID_CLIP_ID;
    });
    if (!changed)
        return false;
    if (engine != nullptr)
        engine->deactivateAllSessionClips();
    else
        tracks.setAllTracksPlaybackMode(TrackPlaybackMode::Arrangement);
    return true;
}

namespace {

int sceneIndexFor(const std::vector<ProjectScene>& scenes, SceneId id) {
    const auto found = std::ranges::find(scenes, id, &ProjectScene::id);
    return found == scenes.end() ? -1 : static_cast<int>(found - scenes.begin());
}

void remapClips(const std::vector<ProjectScene>& before, const std::vector<ProjectScene>& after) {
    auto& clips = ClipManager::getInstance();
    for (const auto& clip : clips.getSessionClips()) {
        if (clip.sceneIndex < 0 || clip.sceneIndex >= static_cast<int>(before.size()))
            continue;
        const auto newIndex =
            sceneIndexFor(after, before[static_cast<std::size_t>(clip.sceneIndex)].id);
        if (newIndex >= 0)
            clips.setClipSceneIndex(clip.id, newIndex);
    }
}

}  // namespace

SessionSceneState SessionApiLive::captureSceneState() const {
    const auto& info = ProjectManager::getInstance().getCurrentProjectInfo();
    return {info.scenes, info.nextSceneId, ClipManager::getInstance().getSessionClips()};
}

void SessionApiLive::restoreSceneState(const SessionSceneState& state) {
    auto& clips = ClipManager::getInstance();
    ClipManager::BatchScope notificationBatch;
    std::unordered_set<ClipId> restoredIds;
    for (const auto& clip : state.clips)
        restoredIds.insert(clip.id);
    for (const auto& clip : clips.getSessionClips()) {
        if (!restoredIds.contains(clip.id))
            clips.deleteClip(clip.id);
    }
    for (const auto& clip : state.clips) {
        if (auto* current = clips.getClip(clip.id)) {
            if (current->trackId != clip.trackId)
                clips.moveClipToTrack(clip.id, clip.trackId);
            clips.setClipSceneIndex(clip.id, clip.sceneIndex);
        } else {
            clips.restoreClip(clip);
        }
    }
    ProjectManager::getInstance().replaceSessionScenes(state.scenes, state.nextSceneId);
}

SceneId SessionApiLive::createScene(int index, const juce::String& name, std::uint32_t colourArgb) {
    ClipManager::BatchScope notificationBatch;
    auto& projects = ProjectManager::getInstance();
    const auto& info = projects.getCurrentProjectInfo();
    if (index < 0 || index > static_cast<int>(info.scenes.size()))
        return INVALID_SCENE_ID;

    auto scenes = info.scenes;
    const auto id = info.nextSceneId;
    scenes.insert(scenes.begin() + index, ProjectScene{id, name, colourArgb});
    const auto before = info.scenes;
    remapClips(before, scenes);
    projects.replaceSessionScenes(std::move(scenes), id + 1);
    return id;
}

bool SessionApiLive::updateScene(SceneId sceneId, const juce::String& name,
                                 std::uint32_t colourArgb) {
    ClipManager::BatchScope notificationBatch;
    auto& projects = ProjectManager::getInstance();
    auto scenes = projects.getCurrentProjectInfo().scenes;
    const auto index = sceneIndexFor(scenes, sceneId);
    if (index < 0)
        return false;
    auto& scene = scenes[static_cast<std::size_t>(index)];
    if (scene.name == name && scene.colourArgb == colourArgb)
        return false;
    scene.name = name;
    scene.colourArgb = colourArgb;
    projects.replaceSessionScenes(std::move(scenes), projects.getCurrentProjectInfo().nextSceneId);
    return true;
}

bool SessionApiLive::moveScene(SceneId sceneId, int toIndex) {
    ClipManager::BatchScope notificationBatch;
    auto& projects = ProjectManager::getInstance();
    const auto& info = projects.getCurrentProjectInfo();
    const auto fromIndex = sceneIndexFor(info.scenes, sceneId);
    if (fromIndex < 0 || toIndex < 0 || toIndex >= static_cast<int>(info.scenes.size()) ||
        fromIndex == toIndex)
        return false;
    const auto before = info.scenes;
    auto scenes = before;
    auto scene = scenes[static_cast<std::size_t>(fromIndex)];
    scenes.erase(scenes.begin() + fromIndex);
    scenes.insert(scenes.begin() + toIndex, std::move(scene));
    remapClips(before, scenes);
    projects.replaceSessionScenes(std::move(scenes), info.nextSceneId);
    return true;
}

SceneId SessionApiLive::duplicateScene(SceneId sceneId, bool copyClips) {
    ClipManager::BatchScope notificationBatch;
    auto& projects = ProjectManager::getInstance();
    const auto& info = projects.getCurrentProjectInfo();
    const auto sourceIndex = sceneIndexFor(info.scenes, sceneId);
    if (sourceIndex < 0)
        return INVALID_SCENE_ID;

    const auto before = info.scenes;
    const auto newSceneId = info.nextSceneId;
    auto scenes = before;
    auto copy = scenes[static_cast<std::size_t>(sourceIndex)];
    copy.id = newSceneId;
    copy.name += " Copy";
    const auto destinationIndex = sourceIndex + 1;
    scenes.insert(scenes.begin() + destinationIndex, std::move(copy));
    remapClips(before, scenes);

    if (copyClips) {
        auto& clips = ClipManager::getInstance();
        for (const auto& clip : clips.getSessionClips()) {
            // Existing rows after the insertion have already shifted, while the
            // source retains its index.
            if (clip.sceneIndex != sourceIndex)
                continue;
            const auto duplicate =
                clips.duplicateClipAtBeats(clip.id, clip.placement.startBeat, clip.trackId, 0.0);
            if (duplicate != INVALID_CLIP_ID)
                clips.setClipSceneIndex(duplicate, destinationIndex);
        }
    }
    projects.replaceSessionScenes(std::move(scenes), newSceneId + 1);
    return newSceneId;
}

bool SessionApiLive::deleteScene(SceneId sceneId, PopulatedScenePolicy policy,
                                 SceneId destinationSceneId) {
    ClipManager::BatchScope notificationBatch;
    auto& projects = ProjectManager::getInstance();
    const auto& info = projects.getCurrentProjectInfo();
    const auto sourceIndex = sceneIndexFor(info.scenes, sceneId);
    if (sourceIndex < 0 || info.scenes.size() <= 1)
        return false;

    auto& clips = ClipManager::getInstance();
    const auto allSessionClips = clips.getSessionClips();
    std::vector<ClipId> sourceClips;
    for (const auto& clip : allSessionClips) {
        if (clip.sceneIndex == sourceIndex)
            sourceClips.push_back(clip.id);
    }
    if (!sourceClips.empty() && policy == PopulatedScenePolicy::Fail)
        return false;

    if (policy == PopulatedScenePolicy::MoveClips) {
        const auto destinationIndex = sceneIndexFor(info.scenes, destinationSceneId);
        if (destinationIndex < 0 || destinationSceneId == sceneId)
            return false;
        for (const auto id : sourceClips) {
            const auto* source = clips.getClip(id);
            if (source != nullptr &&
                clips.getClipInSlot(source->trackId, destinationIndex) != INVALID_CLIP_ID)
                return false;
        }
    }

    const auto before = info.scenes;
    auto scenes = before;
    scenes.erase(scenes.begin() + sourceIndex);
    remapClips(before, scenes);

    if (policy == PopulatedScenePolicy::DeleteClips) {
        for (const auto id : sourceClips)
            clips.deleteClip(id);
    } else if (policy == PopulatedScenePolicy::MoveClips) {
        const auto destinationIndex = sceneIndexFor(scenes, destinationSceneId);
        if (destinationIndex < 0)
            return false;
        for (const auto id : sourceClips)
            clips.setClipSceneIndex(id, destinationIndex);
    }

    projects.replaceSessionScenes(std::move(scenes), info.nextSceneId);
    return true;
}

}  // namespace magda
