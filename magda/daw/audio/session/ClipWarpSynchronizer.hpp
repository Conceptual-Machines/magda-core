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

class ClipWarpSynchronizer {
  public:
    using SessionClipResolver = std::function<tracktion::Clip*(ClipId)>;

    ClipWarpSynchronizer(tracktion::Edit& edit, WarpMarkerManager& warpMarkerManager,
                         const ClipEngineIdMap& clipIds, SessionClipResolver sessionClipResolver);

    void setTransientSensitivity(ClipId clipId, float sensitivity);
    bool getTransientTimes(ClipId clipId);

  private:
    std::map<ClipId, std::string> buildClipMap(ClipId clipId) const;

    tracktion::Edit& edit_;
    WarpMarkerManager& warpMarkerManager_;
    const ClipEngineIdMap& clipIds_;
    SessionClipResolver sessionClipResolver_;
};

}  // namespace magda
