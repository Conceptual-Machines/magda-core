#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "../../core/ClipManager.hpp"
#include "ClipEngineIdMap.hpp"

namespace magda {

class TrackController;

struct ArrangementClipSyncPlan {
    std::vector<ClipId> clipsToRemove;
    std::vector<ClipId> clipsToSync;

    bool hasTopologyChanges() const {
        return !clipsToRemove.empty() || !clipsToSync.empty();
    }
};

ArrangementClipSyncPlan buildArrangementClipSyncPlan(tracktion::Edit& edit,
                                                     TrackController& trackController,
                                                     const std::vector<ClipInfo>& arrangementClips,
                                                     const ClipEngineIdMap& clipIds);

}  // namespace magda
