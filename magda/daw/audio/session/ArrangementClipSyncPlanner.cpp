#include "session/ArrangementClipSyncPlanner.hpp"

#include <unordered_map>
#include <unordered_set>

#include "TrackController.hpp"

namespace magda {

ArrangementClipSyncPlan buildArrangementClipSyncPlan(tracktion::Edit& edit,
                                                     TrackController& trackController,
                                                     const std::vector<ClipInfo>& arrangementClips,
                                                     const std::vector<ClipInfo>& sessionClips,
                                                     const ClipEngineIdMap& clipIds) {
    ArrangementClipSyncPlan plan;

    std::unordered_set<ClipId> currentArrangementClipIds;
    currentArrangementClipIds.reserve(arrangementClips.size());
    for (const auto& clip : arrangementClips)
        currentArrangementClipIds.insert(clip.id);

    std::unordered_set<ClipId> currentSessionClipIds;
    currentSessionClipIds.reserve(sessionClips.size());
    for (const auto& clip : sessionClips)
        currentSessionClipIds.insert(clip.id);

    std::unordered_map<std::string, tracktion::AudioTrack*> engineIdToParentTrack;
    for (auto* track : tracktion::getAudioTracks(edit)) {
        for (auto* teClip : track->getClips())
            engineIdToParentTrack[teClip->itemID.toString().toStdString()] = track;
    }

    for (const auto& entry : clipIds.snapshot()) {
        const auto clipId = entry.first;
        if (currentArrangementClipIds.find(clipId) == currentArrangementClipIds.end() &&
            currentSessionClipIds.find(clipId) == currentSessionClipIds.end())
            plan.clipsToRemove.push_back(clipId);
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
