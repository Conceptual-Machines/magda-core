#pragma once

#include <map>
#include <unordered_map>

#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/LevelTap.hpp"
#include "tap/MidiTap.hpp"

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

    /**
     * @brief Where each Meter op publishes its level.
     *
     * Keyed by the whole OpKey rather than by model ID, because a device's
     * meter and the device itself share a DeviceId and differ only in their
     * role. It is the one binding whose key is the op's rather than the
     * model's, which is what a tap is: a place in the signal rather than a
     * thing in the project.
     *
     * Ordered rather than hashed because OpKey already compares and nothing
     * here is on the audio thread: prepare() resolves these into a flat per-op
     * table and the maps are never touched again.
     *
     * A Meter op with nothing bound is a meter nobody is reading, which is not
     * an error and is not reported as one. Offline render binds none of these,
     * and a plan compiled for a project whose mixer is not on screen binds only
     * what is.
     */
    std::map<OpKey, LevelTap*> meters;

    /**
     * @brief Where a merged-MIDI op publishes what reached it.
     *
     * Keyed by OpKey for the same reason the meters are: this names a place in
     * the signal, not a thing in the project. The place worth binding is a
     * track's TrackMidiInput op, which is everything feeding its chain before
     * any device sees it.
     *
     * Optional exactly like a meter. Nothing binds these in a render nobody is
     * observing, and an unbound op is not reported.
     */
    std::map<OpKey, MidiTap*> midiTaps;
};

}  // namespace magda::engine
