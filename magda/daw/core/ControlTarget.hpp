#pragma once

#include <juce_core/juce_core.h>

#include "ChainNodePath.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Unified address for any parameter the system can write to.
 *
 * Replaces the three legacy addressing schemes (`MacroTarget`, `ModTarget`,
 * `AutomationTarget`, `StaticTarget`) with a single value type. Every consumer
 * — macro/mod links, automation lanes, MIDI bindings, alias resolvers —
 * speaks `ControlTarget`. Resolution to a writable `te::AutomatableParameter*`
 * happens once via `TargetResolver::resolveToParam`.
 *
 * Kind selects which secondary fields are meaningful:
 *
 *  - `PluginParam` — `devicePath` + `paramIndex` (param index into the
 *    plugin's automatable list).
 *  - `DeviceMacro` — `devicePath` + `paramIndex` (macro index on the macro
 *    array attached to the path's owner; Track/Rack/Device determines the
 *    array via `devicePath.getType()`).
 *  - `ModParam`    — `devicePath` (owning scope) + `modId` + `modParamIndex`
 *    (`modParamIndex == 0` is Rate, resolves to `rate` or `rateType` based on
 *    the modifier's tempo-sync flag).
 *  - `TrackVolume` — `devicePath` (track-level path).
 *  - `TrackPan`    — `devicePath` (track-level path).
 *  - `SendLevel`   — `devicePath` (track-level path) + `sendBusIndex`.
 */
struct ControlTarget {
    enum class Kind {
        PluginParam,  // default
        DeviceMacro,
        ModParam,
        TrackVolume,
        TrackPan,
        SendLevel,
    };

    Kind kind = Kind::PluginParam;
    ChainNodePath devicePath;

    int paramIndex = -1;           // PluginParam, DeviceMacro
    ModId modId = INVALID_MOD_ID;  // ModParam
    int modParamIndex = -1;        // ModParam (0 = Rate)
    int sendBusIndex = -1;         // SendLevel

    bool isValid() const {
        if (!devicePath.isValid())
            return false;
        switch (kind) {
            case Kind::PluginParam:
            case Kind::DeviceMacro:
                return paramIndex >= 0;
            case Kind::ModParam:
                return modId != INVALID_MOD_ID && modParamIndex >= 0;
            case Kind::TrackVolume:
            case Kind::TrackPan:
                return true;
            case Kind::SendLevel:
                return sendBusIndex >= 0;
        }
        return false;
    }

    bool operator==(const ControlTarget& other) const {
        if (kind != other.kind || devicePath != other.devicePath)
            return false;
        switch (kind) {
            case Kind::PluginParam:
            case Kind::DeviceMacro:
                return paramIndex == other.paramIndex;
            case Kind::ModParam:
                return modId == other.modId && modParamIndex == other.modParamIndex;
            case Kind::TrackVolume:
            case Kind::TrackPan:
                return true;
            case Kind::SendLevel:
                return sendBusIndex == other.sendBusIndex;
        }
        return false;
    }

    bool operator!=(const ControlTarget& other) const {
        return !(*this == other);
    }

    // Factory helpers — concise construction at call sites.

    static ControlTarget pluginParam(const ChainNodePath& path, int paramIndex) {
        ControlTarget t;
        t.kind = Kind::PluginParam;
        t.devicePath = path;
        t.paramIndex = paramIndex;
        return t;
    }

    static ControlTarget deviceMacro(const ChainNodePath& path, int macroIndex) {
        ControlTarget t;
        t.kind = Kind::DeviceMacro;
        t.devicePath = path;
        t.paramIndex = macroIndex;
        return t;
    }

    static ControlTarget modParam(const ChainNodePath& scopePath, ModId modId, int modParamIndex) {
        ControlTarget t;
        t.kind = Kind::ModParam;
        t.devicePath = scopePath;
        t.modId = modId;
        t.modParamIndex = modParamIndex;
        return t;
    }

    static ControlTarget trackVolume(TrackId track) {
        ControlTarget t;
        t.kind = Kind::TrackVolume;
        t.devicePath = ChainNodePath::trackLevel(track);
        return t;
    }

    static ControlTarget trackPan(TrackId track) {
        ControlTarget t;
        t.kind = Kind::TrackPan;
        t.devicePath = ChainNodePath::trackLevel(track);
        return t;
    }

    static ControlTarget sendLevel(TrackId track, int sendBusIndex) {
        ControlTarget t;
        t.kind = Kind::SendLevel;
        t.devicePath = ChainNodePath::trackLevel(track);
        t.sendBusIndex = sendBusIndex;
        return t;
    }

    // Legacy convenience — extract the leaf device id from devicePath.
    DeviceId deviceId() const {
        return devicePath.getDeviceId();
    }
};

inline const char* toString(ControlTarget::Kind kind) {
    switch (kind) {
        case ControlTarget::Kind::PluginParam:
            return "plugin_param";
        case ControlTarget::Kind::DeviceMacro:
            return "device_macro";
        case ControlTarget::Kind::ModParam:
            return "mod_param";
        case ControlTarget::Kind::TrackVolume:
            return "track_volume";
        case ControlTarget::Kind::TrackPan:
            return "track_pan";
        case ControlTarget::Kind::SendLevel:
            return "send_level";
    }
    return "unknown";
}

}  // namespace magda
