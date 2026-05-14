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

void ClipWarpSynchronizer::enableWarp(ClipId clipId) {
    auto map = buildClipMap(clipId);
    warpMarkerManager_.enableWarp(edit_, map, clipId);
}

void ClipWarpSynchronizer::disableWarp(ClipId clipId) {
    auto map = buildClipMap(clipId);
    warpMarkerManager_.disableWarp(edit_, map, clipId);
}

std::vector<WarpMarkerInfo> ClipWarpSynchronizer::getWarpMarkers(ClipId clipId) {
    auto map = buildClipMap(clipId);
    return warpMarkerManager_.getWarpMarkers(edit_, map, clipId);
}

int ClipWarpSynchronizer::addWarpMarker(ClipId clipId, double sourceTime, double warpTime) {
    auto map = buildClipMap(clipId);
    return warpMarkerManager_.addWarpMarker(edit_, map, clipId, sourceTime, warpTime);
}

double ClipWarpSynchronizer::moveWarpMarker(ClipId clipId, int markerIndex, double newWarpTime) {
    auto map = buildClipMap(clipId);
    return warpMarkerManager_.moveWarpMarker(edit_, map, clipId, markerIndex, newWarpTime);
}

void ClipWarpSynchronizer::removeWarpMarker(ClipId clipId, int markerIndex) {
    auto map = buildClipMap(clipId);
    warpMarkerManager_.removeWarpMarker(edit_, map, clipId, markerIndex);
}

}  // namespace magda
