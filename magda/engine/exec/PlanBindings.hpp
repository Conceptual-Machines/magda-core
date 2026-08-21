#pragma once

#include <map>
#include <unordered_map>

#include "core/TypeIds.hpp"
#include "exec/EngineDevice.hpp"
#include "param/ParamKey.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/LevelTap.hpp"
#include "tap/MidiTap.hpp"
#include "tap/ValueTap.hpp"

namespace magda::engine {

/**
 * @brief The runtime objects a plan's leaf ops resolve to.
 *
 * Keyed by model identity, exactly like the device-bearing part of OpKey, so a
 * binding survives a recompile. DeviceId alone is insufficient because the FX,
 * post-FX and mixer-analysis sections allocate from independent id spaces.
 * Nothing here is owned; the host keeps every object alive for as long as any
 * prepared plan can reference it.
 *
 * Lookups happen once, in PlanExecutor::prepare(), which resolves them into a
 * flat per-op table. The audio thread never touches these maps.
 */
struct PlanBindings {
    std::unordered_map<DeviceKey, EngineDevice*, DeviceKeyHash> devices;

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

    /**
     * @brief Where a value is published for whoever is drawing it (#2122).
     *
     * Keyed by ParamKey rather than by OpKey, which is the one difference from
     * the two above and follows from what is being read: a meter is a place in
     * the signal and this is a number in the parameter system. One map for both
     * of the numbers, because the key already says which: a key with a
     * parameter index is a parameter's resolved position, and a modifier key
     * with none is that modifier's output (ParamKey.hpp).
     *
     * Optional in the same way and for the same reason. A host binds what it is
     * drawing, which is a device editor's worth of parameters out of a
     * project's thousands, and a value nobody has asked about publishes nothing
     * and is not reported.
     *
     * Resolved against the table the plan is prepared with, so a key the table
     * does not carry binds nothing. That is not an error either: a host may ask
     * for a parameter that has since been deleted, and the answer is that
     * nothing writes to it.
     */
    std::map<ParamKey, ValueTap*> valueTaps;
};

}  // namespace magda::engine
