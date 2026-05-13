#pragma once

#include "../../core/DeviceInfo.hpp"
#include "../../core/TypeIds.hpp"

namespace magda {

class PluginManager;
class TrackController;

class SidechainRoutingManager {
  public:
    SidechainRoutingManager(PluginManager& pluginManager, TrackController& trackController);

    void refreshAllSourceMonitors();
    void handleDeviceSidechainChanged(TrackId destinationTrackId, const DeviceInfo& device);
    void triggerMidiActivity(TrackId trackId);
    void publishAudioPeak(TrackId trackId, float peak);

  private:
    PluginManager& pluginManager_;
    TrackController& trackController_;
};

}  // namespace magda
