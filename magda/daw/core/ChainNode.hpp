#pragma once

#include <vector>

#include "MacroInfo.hpp"
#include "ModInfo.hpp"
#include "ParameterInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Which scope of the chain hierarchy a ChainNode points at.
 *
 * Track and Rack nodes own macros + mods. Device nodes additionally own a
 * parameters vector. Tracks share the top-level chain implicitly — children
 * of a Track scope live in TrackInfo::chainElements, not on the node itself.
 */
enum class ChainScope { Track, Rack, Device };

/**
 * @brief Non-owning, mutable view onto the modulation state at a single
 *        node in the Track / Rack / Device hierarchy.
 *
 * Returned by TrackManager::resolveChainNode() given a ChainNodePath. The
 * arrays point into the existing TrackInfo / RackInfo / DeviceInfo storage
 * — this struct introduces no new ownership or copies.
 *
 * Step 1 of issue #1131: every TrackManager macro/mod method that today
 * has a Track/Rack/Device triplet collapses into a single implementation
 * that operates on a ChainNode resolved from the path. The scope-specific
 * triplet methods become thin shims for backward compatibility, to be
 * deleted in step 3.
 */
struct ChainNode {
    ChainScope scope = ChainScope::Track;

    // Owning IDs (used to construct listener notifications). Only the field
    // matching `scope` carries a meaningful value; the others stay invalid.
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    DeviceId deviceId = INVALID_DEVICE_ID;

    // Macros + mods — present at every scope (TrackInfo / RackInfo / DeviceInfo
    // all expose MacroArray macros + ModArray mods as direct fields).
    MacroArray* macros = nullptr;
    ModArray* mods = nullptr;

    // Device-only parameter vector. nullptr for Track / Rack scopes.
    std::vector<ParameterInfo>* params = nullptr;

    bool valid() const {
        return macros != nullptr && mods != nullptr;
    }

    bool isDevice() const {
        return scope == ChainScope::Device;
    }

    // Helpers matching today's macroValueChanged(trackId, isRack, id, ...)
    // listener signature. Track scope uses isRack=false with the trackId in
    // the id field; Device scope uses isRack=false with the deviceId; Rack
    // scope uses isRack=true with the rackId.
    bool isRackMacro() const {
        return scope == ChainScope::Rack;
    }

    int notifyId() const {
        switch (scope) {
            case ChainScope::Track:
                return trackId;
            case ChainScope::Rack:
                return rackId;
            case ChainScope::Device:
                return deviceId;
        }
        return 0;
    }
};

/**
 * @brief Const counterpart of ChainNode for read-only resolution paths.
 */
struct ConstChainNode {
    ChainScope scope = ChainScope::Track;
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    DeviceId deviceId = INVALID_DEVICE_ID;

    const MacroArray* macros = nullptr;
    const ModArray* mods = nullptr;
    const std::vector<ParameterInfo>* params = nullptr;

    bool valid() const {
        return macros != nullptr && mods != nullptr;
    }

    bool isDevice() const {
        return scope == ChainScope::Device;
    }

    bool isRackMacro() const {
        return scope == ChainScope::Rack;
    }

    int notifyId() const {
        switch (scope) {
            case ChainScope::Track:
                return trackId;
            case ChainScope::Rack:
                return rackId;
            case ChainScope::Device:
                return deviceId;
        }
        return 0;
    }
};

}  // namespace magda
