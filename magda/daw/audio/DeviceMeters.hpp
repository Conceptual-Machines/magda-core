#pragma once

#include <map>

#include "../core/ChainNodePath.hpp"
#include "../core/TypeIds.hpp"

/**
 * @file DeviceMeters.hpp
 * @brief What a device slot's meter and a rack's meter read, whichever engine
 *        renders (#2570).
 */

namespace magda {

/**
 * @brief The level each device slot and each rack last reported.
 *
 * The same split TrackMeters is on the track side of: one measurement, held
 * where the chain UI can read it without knowing which engine made it. The
 * fork's per-device levels come out of DeviceMeteringManager and the native
 * engine's off the LevelTap behind each slot's Meter op, and neither is
 * reachable from the other engine's build of the app.
 *
 * Message thread on both sides: an engine's metering timer writes and the
 * chain UI's timers read, both at frame rate. Nothing here is safe from the
 * audio thread, and nothing needs to be -- a tap is what stands between the
 * two.
 */
class DeviceMeters {
  public:
    struct Levels {
        float peakL = 0.0f;
        float peakR = 0.0f;
    };

    void setDevicePeak(const ChainNodePath& devicePath, Levels levels) {
        devices_[devicePath] = levels;
    }

    void setRackPeak(RackId rackId, Levels levels) {
        racks_[rackId] = levels;
    }

    /// False for a slot nothing has reported, which is what a meter with no
    /// engine behind it looks like: the caller leaves its meter alone rather
    /// than drawing a zero over the last level it drew.
    bool devicePeak(const ChainNodePath& devicePath, Levels& out) const {
        const auto found = devices_.find(devicePath);
        if (found == devices_.end())
            return false;

        out = found->second;
        return true;
    }

    bool rackPeak(RackId rackId, Levels& out) const {
        const auto found = racks_.find(rackId);
        if (found == racks_.end())
            return false;

        out = found->second;
        return true;
    }

    /// Called at a project boundary by whichever engine feeds this
    /// (EngineHost::projectTeardown, AudioBridge::projectTeardown): the next
    /// project's devices reuse these ids, and a slot that has not rendered yet
    /// should read nothing rather than whatever the last one left under its
    /// address.
    void clear() {
        devices_.clear();
        racks_.clear();
    }

  private:
    std::map<ChainNodePath, Levels> devices_;
    std::map<RackId, Levels> racks_;
};

}  // namespace magda
