#include "session/ClipWarpSynchronizer.hpp"

#include "WarpMarkerManager.hpp"

namespace magda {

namespace te = tracktion;

ClipWarpSynchronizer::ClipWarpSynchronizer(tracktion::Edit& edit,
                                           WarpMarkerManager& warpMarkerManager,
                                           const ClipEngineIdMap& clipIds,
                                           SessionClipResolver sessionClipResolver)
    : edit_(edit),
      warpMarkerManager_(warpMarkerManager),
      clipIds_(clipIds),
      sessionClipResolver_(std::move(sessionClipResolver)) {}

std::map<ClipId, std::string> ClipWarpSynchronizer::buildClipMap(ClipId clipId) const {
    auto map = clipIds_.snapshot();
    if (map.count(clipId))
        return map;

    if (sessionClipResolver_) {
        if (auto* teClip = sessionClipResolver_(clipId))
            map[clipId] = teClip->itemID.toString().toStdString();
    }

    return map;
}

void ClipWarpSynchronizer::setTransientSensitivity(ClipId clipId, float sensitivity) {
    auto map = buildClipMap(clipId);
    warpMarkerManager_.setTransientSensitivity(edit_, map, clipId, sensitivity);
}

bool ClipWarpSynchronizer::getTransientTimes(ClipId clipId) {
    auto map = buildClipMap(clipId);
    return warpMarkerManager_.getTransientTimes(edit_, map, clipId);
}

}  // namespace magda
