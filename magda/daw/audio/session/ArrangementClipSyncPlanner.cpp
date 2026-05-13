#include "session/ArrangementClipSyncPlanner.hpp"

#include <unordered_map>
#include <unordered_set>

#include "TrackController.hpp"

namespace magda {

ArrangementClipSyncPlan buildArrangementClipSyncPlan(tracktion::Edit& edit,
                                                     TrackController& trackController,
                                                     const std::vector<ClipInfo>& arrangementClips,
                                                     const ClipEngineIdMap& clipIds) {
    ArrangementClipSyncPlan plan;

    std::unordered_set<ClipId> currentClipIds;
    currentClipIds.reserve(arrangementClips.size());
    for (const auto& clip : arrangementClips)
        currentClipIds.insert(clip.id);

    for (const auto& [clipId, engineId] : clipIds.snapshot()) {
        if (currentClipIds.find(clipId) == currentClipIds.end())
            plan.clipsToRemove.push_back(clipId);
    }

    std::unordered_map<std::string, tracktion::AudioTrack*> engineIdToParentTrack;
    for (auto* track : tracktion::getAudioTracks(edit)) {
        for (auto* teClip : track->getClips())
            engineIdToParentTrack[teClip->itemID.toString().toStdString()] = track;
    }

    for (const auto& clip : arrangementClips) {
        auto engineId = clipIds.getEngineId(clip.id);
        if (!engineId) {
            plan.clipsToSync.push_back(clip.id);
            continue;
        }

        auto trackIt = engineIdToParentTrack.find(*engineId);
        auto* expectedTrack = trackController.getAudioTrack(clip.trackId);
        if (trackIt == engineIdToParentTrack.end() || trackIt->second != expectedTrack)
            plan.clipsToSync.push_back(clip.id);
    }

    return plan;
}

}  // namespace magda
