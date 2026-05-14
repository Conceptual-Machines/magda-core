#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../../core/ClipTypes.hpp"
#include "ClipEngineIdMap.hpp"

namespace magda {

class WarpMarkerManager;
struct WarpMarkerInfo;

class ClipWarpSynchronizer {
  public:
    using SessionClipResolver = std::function<tracktion::Clip*(ClipId)>;

    ClipWarpSynchronizer(tracktion::Edit& edit, WarpMarkerManager& warpMarkerManager,
                         const ClipEngineIdMap& clipIds, SessionClipResolver sessionClipResolver);

    void setTransientSensitivity(ClipId clipId, float sensitivity);
    bool getTransientTimes(ClipId clipId);
    void enableWarp(ClipId clipId);
    void disableWarp(ClipId clipId);
    std::vector<WarpMarkerInfo> getWarpMarkers(ClipId clipId);
    int addWarpMarker(ClipId clipId, double sourceTime, double warpTime);
    double moveWarpMarker(ClipId clipId, int markerIndex, double newWarpTime);
    void removeWarpMarker(ClipId clipId, int markerIndex);

  private:
    std::map<ClipId, std::string> buildClipMap(ClipId clipId) const;

    tracktion::Edit& edit_;
    WarpMarkerManager& warpMarkerManager_;
    const ClipEngineIdMap& clipIds_;
    SessionClipResolver sessionClipResolver_;
};

}  // namespace magda
