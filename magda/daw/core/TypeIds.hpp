#pragma once

namespace magda {

// Device identifiers
using DeviceId = int;
constexpr DeviceId INVALID_DEVICE_ID = -1;

/**
 * @brief Which independently allocated device-id space a chain belongs to.
 *
 * Fx is the implicit default for legacy chain paths. Post-fx and mixer-analysis
 * paths carry an explicit segment step.
 */
enum class ChainSegment { Fx, PostFx, MixerAnalysis };

// Mod identifiers
using ModId = int;
constexpr ModId INVALID_MOD_ID = -1;

// Macro identifiers
using MacroId = int;
constexpr MacroId INVALID_MACRO_ID = -1;

// Track identifiers
using TrackId = int;
constexpr TrackId INVALID_TRACK_ID = -1;
constexpr TrackId MASTER_TRACK_ID = -2;  // Well-known ID for master track selection

// Rack identifiers
using RackId = int;
constexpr RackId INVALID_RACK_ID = -1;

// Chain identifiers
using ChainId = int;
constexpr ChainId INVALID_CHAIN_ID = -1;

// Automation identifiers
using AutomationLaneId = int;
constexpr AutomationLaneId INVALID_AUTOMATION_LANE_ID = -1;

using AutomationClipId = int;
constexpr AutomationClipId INVALID_AUTOMATION_CLIP_ID = -1;

using AutomationPointId = int;
constexpr AutomationPointId INVALID_AUTOMATION_POINT_ID = -1;

}  // namespace magda
