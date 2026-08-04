#pragma once

#include <unordered_map>

#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"

namespace magda::engine {

/**
 * @brief The runtime objects a plan's leaf ops resolve to.
 *
 * Keyed by model ID, exactly like OpKey, so a binding survives a recompile: the
 * new plan asks for the same DeviceId and gets the same instance back. Nothing
 * here is owned; the host keeps every object alive for as long as any prepared
 * plan can reference it.
 *
 * Lookups happen once, in PlanExecutor::prepare(), which resolves them into a
 * flat per-op table. The audio thread never touches these maps.
 */
struct PlanBindings {
    std::unordered_map<DeviceId, EngineDevice*> devices;

    /// Arrangement and session playback for a track (ClipAudio / ClipMidi ops).
    std::unordered_map<TrackId, EngineAudioSource*> clipAudio;
    std::unordered_map<TrackId, EngineMidiSource*> clipMidi;

    /// Live hardware input feeding a track (AudioInput / MidiInput ops).
    std::unordered_map<TrackId, EngineAudioSource*> audioInputs;
    std::unordered_map<TrackId, EngineMidiSource*> midiInputs;
};

}  // namespace magda::engine
