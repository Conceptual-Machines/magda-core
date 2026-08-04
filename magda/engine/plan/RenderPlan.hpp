#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/TypeIds.hpp"

/**
 * @file RenderPlan.hpp
 * @brief The native engine's render plan IR.
 *
 * A render plan is a flat, dependency-ordered list of ops. It encodes signal
 * topology only: tracks, devices, routing, sends. Clip positions, automation
 * curves, modulation and parameter values are NOT in the plan: ops read those
 * through separately swapped snapshots, so editing them never triggers a
 * compile. Structural recompiles therefore happen only at human speed.
 *
 * A published plan is immutable. Every structural change compiles a new plan
 * and swaps it in atomically; the differ carries runtime state across the swap
 * by matching ops on their OpKey.
 */

namespace magda::engine {

/** Index of an op inside RenderPlan::ops. */
using OpId = int;
constexpr OpId INVALID_OP_ID = -1;

/** Which kind of signal a port carries. */
enum class SignalKind : std::uint8_t { Audio, Midi };

/**
 * @brief What an op computes.
 *
 * The vocabulary is deliberately small: anything a device does beyond
 * processing (its gain trim, its meter tap) is its own op, so the differ can
 * carry, rebuild and crossfade those pieces independently. Fusing adjacent
 * cheap ops is a later back-end pass over the same flat list.
 */
enum class OpKind : std::uint8_t {
    ClipAudio,   ///< audio clip playback for one track (reads the clip snapshot)
    ClipMidi,    ///< MIDI clip playback for one track
    AudioInput,  ///< live hardware audio input
    MidiInput,   ///< live MIDI input
    Device,      ///< a device instance (instrument, effect, MIDI or analysis)
    MixAudio,   ///< ordered sum of audio inputs (summing order is compiled, never scheduling order)
    MergeMidi,  ///< ordered merge of MIDI inputs
    Gain,       ///< scalar gain
    Fader,      ///< volume + pan, and the MIDI its stage passes on
    SendTap,    ///< pre/post-fader tap feeding another track
    Meter,      ///< level tap read by the UI
    Output,     ///< hardware output
};

/**
 * @brief The op's structural role at its model location.
 *
 * Model ID alone does not identify an op: one device contributes a process op,
 * a gain op and a meter op, all keyed on the same DeviceId. The role is the
 * second half of the differ's identity key.
 */
enum class OpRole : std::uint8_t {
    ClipAudio,        ///< the track's audio clip source
    ClipMidi,         ///< the track's MIDI clip source
    LiveAudioInput,   ///< the track's live audio input
    LiveMidiInput,    ///< the track's live MIDI input
    TrackAudioInput,  ///< sum of everything feeding the track's chain head
    TrackMidiInput,   ///< merge of everything feeding the track's chain head
    DeviceProcess,    ///< the device itself
    DeviceGain,       ///< the device slot's gain trim
    DeviceMeter,      ///< the device slot's level tap
    ChainMidiMerge,   ///< raw chain MIDI merged with a device's MIDI output
    RackChainFader,   ///< one rack chain's volume + pan
    RackMix,          ///< sum of a rack's chains
    RackMidiMix,      ///< merge of a rack's chain MIDI outputs
    RackFader,        ///< the rack's output volume + pan
    TrackFader,       ///< the track fader
    TrackMeter,       ///< the track's post-fader, pre-mute level tap
    TrackMute,        ///< mute and solo, applied after the meter and sidechain tap
    SendTap,          ///< one send slot
    HardwareOutput,   ///< the master's hardware output
};

/**
 * @brief Whether an op's output is computable ahead of the transport.
 *
 * Deterministic ops depend only on the model, the transport position and
 * parameter lanes, so an anticipative executor may render them ahead of time.
 * Live ops depend on the outside world (hardware input) and can never be run
 * early; liveness propagates downstream from a live source. Carried on every op
 * from day one so the anticipative executor does not need a new plan format.
 */
enum class LivenessDomain : std::uint8_t { Deterministic, Live };

/**
 * @brief An op's identity across plan swaps.
 *
 * The differ hash-joins old and new plans on this key. It is a flat value type
 * on purpose: no heap, comparable and hashable, so identity work never
 * allocates. Only the fields relevant to the op's location are set; the rest
 * stay invalid.
 */
struct OpKey {
    TrackId trackId = INVALID_TRACK_ID;
    RackId rackId = INVALID_RACK_ID;
    ChainId chainId = INVALID_CHAIN_ID;
    DeviceId deviceId = INVALID_DEVICE_ID;
    OpRole role = OpRole::TrackAudioInput;
    /// Disambiguates repeats of the same role at the same location (send slot).
    int index = 0;

    bool operator==(const OpKey& o) const {
        return trackId == o.trackId && rackId == o.rackId && chainId == o.chainId &&
               deviceId == o.deviceId && role == o.role && index == o.index;
    }
    bool operator!=(const OpKey& o) const {
        return !(*this == o);
    }
    bool operator<(const OpKey& o) const;
};

/** A reference to one output port of an earlier op. */
struct PortRef {
    OpId op = INVALID_OP_ID;
    int port = 0;

    bool valid() const {
        return op != INVALID_OP_ID;
    }
    bool operator==(const PortRef& o) const {
        return op == o.op && port == o.port;
    }
};

/** An unconnected input slot. Legal wherever the op's arity declares a slot
 *  that this model configuration does not fill (an effect with no MIDI input,
 *  a device with no sidechain source). */
constexpr PortRef noInput() {
    return PortRef{};
}

/**
 * @brief One node of the render plan.
 *
 * Input arity is fixed per OpKind (see arityOf), so slot meaning is positional
 * and stable: unfilled slots hold an invalid PortRef rather than shifting the
 * remaining inputs down.
 */
struct PlanOp {
    OpKind kind = OpKind::MixAudio;
    OpKey key;
    LivenessDomain liveness = LivenessDomain::Deterministic;
    std::vector<PortRef> inputs;
    std::vector<SignalKind> outputs;
};

/**
 * @brief A compiled, immutable render plan.
 *
 * Ops are stored in dependency order: every input references an op at a lower
 * index. That makes the reference executor a straight walk of the vector, and
 * lets the parallel executor's scheduling constants be baked here at compile
 * time rather than rebuilt on the audio thread every block.
 */
struct RenderPlan {
    static constexpr int kVersion = 1;

    int version = kVersion;
    std::vector<PlanOp> ops;

    /// Ops that drive hardware; the block is finished when these have run.
    std::vector<OpId> outputOps;

    // --- Scheduling constants, baked by bakeScheduling() ---

    /// Per op: how many distinct producer ops it waits on. The executor memcpys
    /// this into its live countdown array per block instead of walking the graph.
    std::vector<std::uint16_t> dependencyCounts;

    /// Reversed edges in CSR form: consumers of op i are
    /// consumerEdges[consumerOffsets[i] .. consumerOffsets[i + 1]).
    std::vector<int> consumerOffsets;
    std::vector<OpId> consumerEdges;

    /// Ops with no dependencies, seeded into the ready queue at block start.
    std::vector<OpId> initialReadyOps;

    /// Model configurations the compiler could not express, in compile order.
    /// Never a silent drop: anything the plan does not implement says so here.
    std::vector<std::string> diagnostics;
};

/**
 * @brief Fixed number of input slots for an op kind, or -1 when variadic.
 *
 * Device slots are [audio, MIDI, sidechain audio].
 */
int arityOf(OpKind kind);

/** Fill in dependencyCounts, consumer edges and initialReadyOps. */
void bakeScheduling(RenderPlan& plan);

/**
 * @brief Structural identity of a plan, over its ops, keys and edges.
 *
 * Anything resolved against a plan and published separately from it (values,
 * snapshots) carries this so the audio thread can tell that the two belong
 * together. Op count is not identity: a structural edit can replace or reorder
 * ops and keep the count, and a stale table applied by index would then put one
 * op's gain or mute on another. Two plans that compile to the same structure
 * hash the same, which is correct: values resolved for either one fit both.
 */
std::uint64_t planFingerprint(const RenderPlan& plan);

/**
 * @brief Structural checks over a compiled plan.
 *
 * Returns one message per violation, empty when the plan is well formed.
 * Checks dependency ordering, input arity, port existence and signal-kind
 * agreement between producer and consumer.
 *
 * It also enforces the differ's two preconditions, which are cheap here and
 * expensive anywhere else: OpKey uniqueness, because the differ hash-joins on
 * the key and a collision carries one op's state into another rather than
 * failing; and liveness provenance in both directions, because an over-tagged
 * Live op is silently correct while quietly shrinking what the anticipative
 * executor may precompute.
 */
std::vector<std::string> validatePlan(const RenderPlan& plan);

const char* toString(OpKind kind);
const char* toString(OpRole role);
const char* toString(SignalKind kind);
const char* toString(LivenessDomain domain);

/** Canonical key text, e.g. "T1/R4/C5/D7:deviceProcess" or "T-2:trackFader". */
std::string toString(const OpKey& key);

}  // namespace magda::engine
