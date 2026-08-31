#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <tuple>
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
 *
 * Not everything in a plan came from the compiler. A plan published while an
 * edit is being faded in carries Crossfade ops the compiler never emitted, put
 * there by a pass over its output (see PlanCrossfade.hpp); they are ordinary
 * ops in every other respect, and they leave again at the next publish.
 *
 * Latency compensation splits along the same seam. Where the delay lines go is
 * topology and lives here as Delay ops; how many samples each one holds is a
 * property of a loaded plugin, which the model cannot know and the compiler has
 * never seen, so it is resolved when the plan is prepared against its bindings.
 * A plugin whose reported latency changes therefore re-prepares rather than
 * recompiling, and the plan, its fingerprint and everything keyed on an OpKey
 * come through untouched.
 */

namespace magda::engine {

/** Index of an op inside RenderPlan::ops. */
using OpId = int;
constexpr OpId INVALID_OP_ID = -1;

/** Which kind of signal a port carries. */
enum class SignalKind : std::uint8_t { Audio, Midi };

/**
 * @brief One output port: what it carries, and for audio, how wide.
 *
 * The bus is stereo, so a port is at most stereo. A mono port is one channel
 * that every reader hears on both sides of its slot, the way the current
 * engine reads a one-pin source off pin 1 twice. The producing op copies it
 * across, so nothing downstream has to know. Zero channels means the port
 * carries nothing; it is kept so port positions do not shift when a device
 * reports no audio output.
 */
struct PortDesc {
    SignalKind kind = SignalKind::Audio;
    std::uint8_t channels = 2;

    // Implicit, so existing {SignalKind::Audio} port lists still mean what they
    // did: a bare kind is the bus at full width.
    PortDesc(SignalKind k = SignalKind::Audio)
        : kind(k), channels(k == SignalKind::Audio ? 2 : 0) {}
    PortDesc(SignalKind k, std::uint8_t c) : kind(k), channels(c) {}

    bool operator==(const PortDesc&) const = default;
};

// Output ports of a Device op, in order. Port 0 is the device's audio output,
// and it is the one the chain carries on from. A device that writes MIDI has it
// at port 1. Anything after that is a multi-out instrument's further output
// pairs, in pair order, and those leave the chain rather than continuing along
// it: each is read by the MultiOut track whose MultiOutTrackLink names that
// pair, the way the current engine gives the pair its own RackInstance reading
// its own pins. The order is what makes the two existing shapes, {Audio} and
// {Audio, Midi}, special cases of it rather than something a reader has to tell
// apart.

/// Output pairs past the main one that one Device op may carry.
///
/// Not a limit the model has. `InstrumentRackManager::wrapMultiOutInstrument`
/// adds one rack pin per channel with no clamp, and a device's pairs are read
/// off the plugin's own buses, so the model will describe as many as the plugin
/// reports. (The clamp in `RackSyncManager` is the user-rack chain mechanism,
/// which is unrelated: see the aux-output note in PlanCompiler::emitRack.)
///
/// What is bounded is the executor, which gathers a device's pair blocks on the
/// stack because the parallel executor runs Device ops on any thread and shared
/// storage would be two devices writing one array. This is that budget, and it
/// is generous next to the widest plugins in use: 64 pairs is 128 channels.
/// A device with more is reported by the compiler rather than quietly shortened.
constexpr int kMaxMultiOutPairs = 64;

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
    MidiNoteGate,  ///< passes a note range through, transposed onto a root
    Subtract,      ///< one audio input minus another: what a delta solo hears
    Delay,         ///< latency compensation on one edge; the sample count is bound at prepare time
    Crossfade,     ///< an edge as it was and as it is, ramped from one to the other
    Gain,          ///< scalar gain
    Fader,         ///< volume + pan, and the MIDI its stage passes on
    SendTap,       ///< pre/post-fader tap feeding another track
    Meter,         ///< level tap read by the UI
    ModSource,     ///< a track's signal as the modulation system reads it
    Output,        ///< hardware output

    // A hardware insert, which is these two with the outside world between them
    // (#2245). Not a device with special cases: the incumbent recognises an
    // insert by where it sits in a chain, and the plan already has ops for
    // things that consume a signal and things that produce one.
    InsertSend,    ///< audio or MIDI leaving the machine; consumes, produces nothing
    InsertReturn,  ///< what comes back, and where the round trip's latency is declared
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
    DeviceInject,     ///< an instrument's output summing into the bus flowing past it
    DeviceDelta,      ///< the device's output minus the dry input it was handed
    DeviceGain,       ///< the device slot's gain trim
    DeviceMeter,      ///< the device slot's level tap
    ChainMidiMerge,   ///< raw chain MIDI merged with a device's MIDI output
    PadNoteGate,      ///< one pad chain's note range and transposition
    RackChainFader,   ///< one rack chain's volume + pan
    RackMix,          ///< sum of a rack's chains
    RackMidiMix,      ///< merge of a rack's chain MIDI outputs
    RackFader,        ///< the rack's output volume + pan
    RackDelta,        ///< the rack's output minus the dry input it was handed
    TrackFader,       ///< the track fader
    TrackMeter,       ///< the track's post-fader, pre-mute level tap
    TrackMute,        ///< mute and solo, applied after the meter and sidechain tap
    SendTap,          ///< one send slot
    ModulationTap,    ///< one track's signal, read by the modifiers listening to it
    HardwareOutput,   ///< the master's hardware output
    InsertSend,       ///< one insert's send
    InsertReturn,     ///< one insert's return

    // Latency compensation. A delay sits on one edge, so its identity is the
    // op it feeds plus the input slot it fills: the role says which op that is,
    // and OpKey::index says which slot. Two ops of the same kind never share a
    // location, so this is unique wherever the compiler emits it, which
    // validatePlan's key check is what actually guarantees.
    MixInputDelay,       ///< aligns one input of a MixAudio
    MergeInputDelay,     ///< aligns one input of a MergeMidi
    DeviceInputDelay,    ///< aligns one input slot of a Device
    FaderInputDelay,     ///< aligns one input slot of a Fader
    SubtractInputDelay,  ///< aligns one input slot of a Subtract

    // A fade sits on one edge, like a delay, so its identity is the op it feeds
    // plus the slot it fills. Unlike a delay it has no role of its own to say
    // which op that is, because it can land on any of them, so everything that
    // identifies the edge goes into OpKey::index instead (see crossfadeIndex).
    // Two fades at one model location, on a fader and on the meter behind it,
    // would otherwise be the same key and the differ would carry one into the
    // other; so would two send taps on one track, which differ by nothing but
    // the index of their slot.
    EdgeCrossfade,  ///< ramps one input slot of the op it is keyed to
};

// The four things that identify a fade, packed into OpKey::index, low bits
// first. Everything about the edge it sits on is in there, because the location
// fields of the key belong to the op it feeds and two ops at one location can
// both be faded at once.
//
// Depth is what keeps a fade that is still running distinct from the one
// stacked in front of it. A second edit arriving mid-fade cannot use the first
// fade's destination as its old side without stepping to it, so the running
// fade stays in the plan as the new one's old side, and the pair needs two
// keys. It only ever grows outwards: the fade already there keeps its depth,
// and therefore its ramp, for as long as it runs.
constexpr unsigned kCrossfadeSlotBits = 10;
constexpr unsigned kCrossfadeIndexBits = 12;
constexpr unsigned kCrossfadeDepthBits = 3;

/// One past the largest slot, consumer index and depth a fade's key can hold.
constexpr int kCrossfadeMaxSlot = static_cast<int>(1U << kCrossfadeSlotBits);
constexpr int kCrossfadeMaxIndex = static_cast<int>(1U << kCrossfadeIndexBits);
constexpr int kCrossfadeMaxDepth = static_cast<int>(1U << kCrossfadeDepthBits);

/**
 * @brief The key index of a fade on one edge.
 *
 * @param consumerRole   role of the op the fade feeds
 * @param consumerIndex  that op's own key index, which is what tells two send
 *                       taps on one track apart
 * @param slot           the input slot of that op the fade fills
 * @param depth          how many fades are already stacked on this edge
 */
constexpr int crossfadeIndex(OpRole consumerRole, int consumerIndex, int slot, int depth) {
    constexpr auto depthShift = kCrossfadeIndexBits + kCrossfadeSlotBits;
    constexpr auto roleShift = kCrossfadeDepthBits + depthShift;
    return static_cast<int>((static_cast<unsigned>(consumerRole) << roleShift) |
                            (static_cast<unsigned>(depth) << depthShift) |
                            (static_cast<unsigned>(consumerIndex) << kCrossfadeSlotBits) |
                            static_cast<unsigned>(slot));
}

/** The role of the op a fade with this key index feeds. */
constexpr OpRole crossfadeConsumerRole(int index) {
    return static_cast<OpRole>(static_cast<unsigned>(index) >>
                               (kCrossfadeDepthBits + kCrossfadeIndexBits + kCrossfadeSlotBits));
}

/** How many fades this one is stacked on. */
constexpr int crossfadeDepth(int index) {
    return static_cast<int>(
        (static_cast<unsigned>(index) >> (kCrossfadeIndexBits + kCrossfadeSlotBits)) &
        static_cast<unsigned>(kCrossfadeMaxDepth - 1));
}

/** The key index of the op a fade with this key index feeds. */
constexpr int crossfadeConsumerIndex(int index) {
    return static_cast<int>((static_cast<unsigned>(index) >> kCrossfadeSlotBits) &
                            static_cast<unsigned>(kCrossfadeMaxIndex - 1));
}

/** The input slot a fade with this key index fills. */
constexpr int crossfadeSlot(int index) {
    return static_cast<int>(static_cast<unsigned>(index) &
                            static_cast<unsigned>(kCrossfadeMaxSlot - 1));
}

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
 * @brief Stable identity of one device instance in the project model.
 *
 * DeviceId is allocated independently for the main FX, post-FX and mixer-
 * analysis sections. It is unique inside one section, across tracks and rack
 * chains, but the same integer may name one device in each section. Runtime
 * ownership therefore needs the section as well as the integer.
 */
struct DeviceKey {
    ChainSegment segment = ChainSegment::Fx;
    DeviceId deviceId = INVALID_DEVICE_ID;

    DeviceKey() = default;
    explicit DeviceKey(DeviceId id) : deviceId(id) {}
    DeviceKey(ChainSegment section, DeviceId id) : segment(section), deviceId(id) {}

    bool operator==(const DeviceKey& o) const {
        return segment == o.segment && deviceId == o.deviceId;
    }
    bool operator!=(const DeviceKey& o) const {
        return !(*this == o);
    }
    bool operator<(const DeviceKey& o) const {
        return std::tie(segment, deviceId) < std::tie(o.segment, o.deviceId);
    }
};

struct DeviceKeyHash {
    std::size_t operator()(const DeviceKey& key) const noexcept {
        auto seed = std::hash<DeviceId>{}(key.deviceId);
        const auto segment = std::hash<unsigned>{}(static_cast<unsigned>(key.segment));
        seed ^= segment + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

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
    /// Which independently allocated device-id space this location belongs to.
    ChainSegment segment = ChainSegment::Fx;

    DeviceKey deviceKey() const {
        return DeviceKey{segment, deviceId};
    }

    bool operator==(const OpKey& o) const {
        return trackId == o.trackId && rackId == o.rackId && chainId == o.chainId &&
               deviceId == o.deviceId && role == o.role && index == o.index && segment == o.segment;
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
    std::vector<PortDesc> outputs;

    /// Channels of audio a Device op reads from its first input port. The port
    /// belongs to the chain and stays stereo; a device declaring one input
    /// channel gets only the first. That matches the current engine, which
    /// wires pin 1 and leaves pin 2 unconnected. It is not a downmix. Zero for
    /// every other op kind, and for a device not connected to the bus at all.
    std::uint8_t audioInputChannels = 0;

    /// The notes a MidiNoteGate passes, inclusive, and the semitone shift it
    /// applies to what it passes.
    ///
    /// Topology rather than a value: a note range is not something a mixer move
    /// can touch, so it belongs here beside audioInputChannels rather than in
    /// the value table, and editing a pad's range recompiles the way adding a
    /// device does. `noteGateTranspose` is `rootNote - lowNote`, resolved at
    /// compile time so the audio thread adds one number instead of doing the
    /// arithmetic per event. Zero for every other op kind.
    std::uint8_t noteGateLow = 0;
    std::uint8_t noteGateHigh = 127;
    std::int8_t noteGateTranspose = 0;

    /// A pad fader's level and pan, as parameter indices of the device that
    /// owns the pad (OpKey::deviceId).
    ///
    /// A rack chain's fader is not addressable in the model and keeps whatever
    /// the value table published. A Drum Grid's pads are the exception: their
    /// level and pan are real automatable parameters of the Drum Grid, so a lane,
    /// a macro or a modifier can play over them and the fader has to read the
    /// table like a track's does. -1 for every other fader.
    int padLevelParam = -1;
    int padPanParam = -1;
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

/** The scheduling constants a plan's topology implies. */
struct PlanScheduling {
    std::vector<std::uint16_t> dependencyCounts;
    std::vector<int> consumerOffsets;
    std::vector<OpId> consumerEdges;
    std::vector<OpId> initialReadyOps;
};

/**
 * @brief Work out the scheduling constants for @p plan, without writing them.
 *
 * A pure function of the ops and their inputs, which is what lets the executor
 * check a plan's constants rather than take them on trust.
 */
PlanScheduling scheduleOf(const RenderPlan& plan);

/** Fill in dependencyCounts, consumer edges and initialReadyOps. */
void bakeScheduling(RenderPlan& plan);

/**
 * @brief Whether a plan's baked constants are the ones its topology implies.
 *
 * Not a size check. The parallel executor's whole safety story is that the
 * schedule it copies in every block came from the compiler, and every way of
 * being wrong here is expensive: a count too high or a seed op missing leaves
 * ops nobody will ever release, so the block never finishes and the callback
 * spins for ever; a count too low releases an op while its producers are still
 * writing, which is a race on a shared buffer; an offset or an edge out of
 * range walks off the end of an array. None of it is visible to validatePlan,
 * which reads the topology, and none of it is in the fingerprint, which is
 * computed from the topology too.
 *
 * So the constants are recomputed and compared. It runs off the audio thread,
 * once per prepare, and it is the same linear pass that produced them.
 */
bool carriesSchedule(const RenderPlan& plan);

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
 *
 * Delays get the same treatment: one edge each, no stacking, and a key that
 * names the slot they fill. Latency resolution reads a delay's count off the op
 * it feeds, which is only well defined while that holds.
 */
std::vector<std::string> validatePlan(const RenderPlan& plan);

const char* toString(OpKind kind);
const char* toString(OpRole role);
const char* toString(SignalKind kind);
const char* toString(LivenessDomain domain);

/** Canonical device identity text, e.g. "D7", "PostFx/D7". */
std::string toString(const DeviceKey& key);
std::ostream& operator<<(std::ostream& out, const DeviceKey& key);

/** Canonical key text, e.g. "T1/R4/C5/D7:deviceProcess" or "T1/PF/D7:deviceProcess". */
std::string toString(const OpKey& key);

}  // namespace magda::engine
