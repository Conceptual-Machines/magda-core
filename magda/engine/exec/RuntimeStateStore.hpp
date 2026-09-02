#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "clip/ClipSnapshot.hpp"
#include "exec/PlanBindings.hpp"
#include "launch/LaunchHandle.hpp"
#include "launch/SessionLauncher.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/LevelTap.hpp"
#include "tap/ValueTap.hpp"

namespace magda {
struct TrackInfo;
}

/**
 * @file RuntimeStateStore.hpp
 * @brief Who owns the objects a plan renders through, and for how long.
 *
 * A plan is topology and nothing else, so it cannot own a plugin instance or a
 * file reader: it is rebuilt every time a device moves, and rebuilding an
 * instrument because a fader was reordered above it is the rebuild-click
 * problem in miniature. The store owns them instead, keyed by section-aware
 * model identity the way OpKey is, so the same edit that recompiles the plan
 * leaves the objects it names untouched.
 *
 * Everything here runs off the audio thread.
 */

namespace magda::engine {

struct ParamTable;

/**
 * @brief Makes the runtime objects a plan asks for.
 *
 * Implemented by the host, because the engine has no idea what a device is: it
 * knows a section-aware DeviceKey, and the host knows which plugin that is.
 * Returning nullptr is allowed and means the object does not exist; the
 * executor reports the op as unbound rather than pretending otherwise.
 */
class RuntimeStateFactory {
  public:
    virtual ~RuntimeStateFactory() = default;

    virtual std::unique_ptr<EngineDevice> createDevice(DeviceKey) {
        return nullptr;
    }
    virtual std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) {
        return nullptr;
    }
    virtual std::unique_ptr<EngineMidiSource> createClipMidiSource(TrackId) {
        return nullptr;
    }
    virtual std::unique_ptr<EngineAudioSource> createSessionAudioSource(TrackId) {
        return nullptr;
    }
    virtual std::unique_ptr<EngineMidiSource> createSessionMidiSource(TrackId) {
        return nullptr;
    }
    virtual std::unique_ptr<EngineAudioSource> createAudioInput(TrackId) {
        return nullptr;
    }
    virtual std::unique_ptr<EngineMidiSource> createMidiInput(TrackId) {
        return nullptr;
    }

    /**
     * @brief The level tap behind one Meter op, or nullptr for one nobody reads.
     *
     * Declining is the ordinary answer rather than a failure: a plan has a
     * meter at every track, every device slot and the master, and a host with
     * no mixer on screen wants none of them. What it costs to decline is the
     * meter, and nothing else: the op still renders, because a Meter op is a
     * point in the signal path first and a display second.
     *
     * Handed the whole key because a device's meter and the device itself share
     * a DeviceId. Which track or device the tap belongs to is read off the key,
     * and so is what kind of meter it is.
     */
    virtual std::unique_ptr<LevelTap> createMeter(const OpKey&) {
        return nullptr;
    }

    /**
     * @brief The values the host wants read back (#2122).
     *
     * Asked once per plan publish, off the audio thread. Every key returned
     * gets a ValueTap it can read at whatever rate it draws: a parameter's key
     * for the position that parameter resolved to, a modifier's for the output
     * that modifier published.
     *
     * A modifier is named by its key with no parameter index, which means
     * `index = -1` and not the default a hand-built ParamKey carries: index 0
     * is the modifier's own Rate parameter, so a key left at its default asks
     * about the rate rather than the output. modifierKeyFor() builds the right
     * shape from a ControlTarget and is the way to avoid choosing.
     *
     * Shaped as one question rather than as a createValueTap() beside the
     * others, which is the one asymmetry here and is a matter of how many there
     * are. A plan has hundreds of ops and a host with a mixer open wants a
     * meter on a good fraction of them, so asking op by op costs about what the
     * answers are worth. A table has a parameter per parameter of every device
     * in the project, which is thousands, and a host wants the few dozen it has
     * on screen: asking one by one would be thousands of declines per edit to
     * find them. So the host says what it wants and the store makes them.
     *
     * The store makes them, rather than this handing back instances, because a
     * ValueTap is not a thing the engine needs the host's help to build. The
     * methods above exist because the engine has no idea what a device or a
     * file reader is; it knows exactly what a tap on a number is.
     *
     * Returning nothing is the ordinary answer. An offline render wants none of
     * these, and neither does a session whose windows are all shut.
     */
    virtual std::vector<ParamKey> valuesToTap() {
        return {};
    }
};

/**
 * @brief Model IDs that still exist, whatever the current plan happens to use.
 *
 * Eviction is model-aware rather than plan-aware, and the difference is not
 * academic: bypass and chain power are structural, so a bypassed device
 * contributes no ops at all. Keyed on the plan, switching chain power off would
 * tear down every plugin on the track and switching it back on would rebuild
 * them, losing tails, plugin state and load time on a gesture the user expects
 * to be free. Plan-named means playing, model-named means kept, and only
 * deletion from the model destroys anything.
 */
struct RuntimeStateIds {
    std::set<DeviceKey> devices;
    std::set<TrackId> tracks;

    /**
     * @brief Every session slot the model holds, when the caller knows them.
     *
     * Absent rather than empty when it does not. Slots are clips, and the walk
     * that answers what exists is handed tracks, so a caller with no snapshot
     * to hand cannot name them; absent means "keep by track", which leaks a
     * handle rather than retiring one something may still be holding.
     *
     * Track granularity is not enough on its own. A slot emptied while its
     * track remains would keep its handle for ever, so the next clip put in
     * that scene would inherit the old one's play state, loop phase and played
     * range (#2301).
     */
    std::optional<std::set<SlotKey>> slots;
};

/**
 * @brief Every device and track the model holds, nested racks included.
 *
 * Deliberately not the compiler's walk: that one answers what plays, this one
 * answers what exists, and the gap between the two is exactly what gets kept.
 */
RuntimeStateIds collectRuntimeStateIds(const std::vector<TrackInfo>& tracks,
                                       const TrackInfo& master);

/**
 * @brief The same, with the session slots @p clips says exist.
 *
 * The snapshot rather than the model because that is what enumerates slots, and
 * because it is what a handle would be made for: a slot the snapshot does not
 * carry has nothing to launch.
 */
RuntimeStateIds collectRuntimeStateIds(const std::vector<TrackInfo>& tracks,
                                       const TrackInfo& master, const ClipSnapshot& clips);

/**
 * @brief The runtime objects behind a plan's leaf ops, owned across swaps.
 */
class RuntimeStateStore {
  public:
    explicit RuntimeStateStore(RuntimeStateFactory& factory) : factory_(factory) {}

    /**
     * @brief Bindings for @p plan, creating what is new and reusing what is not.
     *
     * Never destroys anything: a plan being prepared is not published yet, and
     * until it is, the audio thread is still rendering the previous one.
     *
     * Preparing an object is part of creating it and happens nowhere else, for
     * the same reason. A device the store already holds is one the live plan
     * may be inside the process() of this instant, and prepare() on a real
     * plugin resizes and clears what process() is reading: it is a race, and on
     * the runs where it is not, it is the tail, the delay line and the filter
     * state the swap exists to keep. So an object is prepared once, when it is
     * new, before anything can reach it.
     *
     * @p context changing is not a live operation. Every object the store holds
     * is prepared again for it, which is only safe because a sample rate or
     * block size change means the audio device has stopped and there is no
     * callback to race.
     */
    PlanBindings realise(const RenderPlan& plan, const RenderContext& context);

    /**
     * @brief Destroy what neither @p livePlan nor @p modelIds names.
     *
     * Two keep sets, for two different reasons, and the plan's is the one that
     * cannot be waived. Anything @p livePlan names is reachable from the audio
     * thread right now, so destroying it is a use-after-free rather than an
     * early eviction; taking the plan here rather than trusting the caller's
     * IDs is what stops that from depending on the two arguments agreeing.
     * They will not always agree: plans are compiled from a model revision and
     * published later, so a caller collecting IDs from the current model can
     * hand over a set that has already lost something the plan still uses.
     *
     * @p modelIds only ever extends retention, to what exists but is not
     * playing: a bypassed device, a chain with the power off, the input source
     * of a track nobody has armed.
     *
     * Call only after the swap, with the plan that is now live. Destructors
     * run on the calling thread.
     *
     * @p liveTable is the parameter table that plan is rendering with, and it
     * is to the value taps what the plan is to everything else: the one thing
     * that says which of them the audio thread can reach. A plan does not name
     * a parameter, so nothing else here could answer it. Null is a session
     * rendering without a table, where no value tap is reachable at all.
     *
     * Required rather than defaulted, for the reason the plan is passed rather
     * than trusted to the caller's IDs. Omitting it would leave eviction to the
     * model IDs alone, and a tap the live table carries but the ID set does not
     * name would be destroyed while the executor still holds its pointer: the
     * next block writes through freed memory, on the audio thread. A keep that
     * cannot be waived must not be possible to leave out.
     */
    std::size_t releaseDeleted(const RenderPlan& livePlan, const RuntimeStateIds& modelIds,
                               const ParamTable* liveTable);

    /**
     * @brief The tap publishing @p key, or nullptr (#2122).
     *
     * The other half of valuesToTap(): the host says which values it wants read
     * back and this is where it collects them. Off the audio thread; what it
     * hands back is readable from any thread, which is the whole point of it.
     *
     * Null until a publish has been attempted, because that is when the taps
     * are made: a window that opens between two of them is drawing a value
     * nothing has offered yet, and it gets one at the next publish rather than
     * never. Attempted rather than succeeded, since the taps are realised
     * before a plan is known to prepare; non-null therefore says the store has
     * one, not that anything is publishing through it. What answers that is the
     * tap's own count, where zero means nothing in the engine moves this value
     * and the model's own is the answer (ValueTap.hpp).
     *
     * Good until the next publish, like every other pointer the store hands
     * out. A tap whose device has been deleted goes when the plan that named it
     * stops being live, and a caller holding one across that has a pointer to
     * nothing; asking again is how a holder finds out.
     */
    ValueTap* valueTap(const ParamKey& key) const;

    /**
     * @brief Make a handle for every slot @p clips names and publish them.
     *
     * On the publishing thread, beside the snapshot the slots came out of: a
     * handle is made for a slot that has none and kept for a slot that already
     * had one, which is what makes a clip edit leave a playing slot alone.
     *
     * The publish happens here rather than at the caller because what is
     * published is also the keep set eviction may not waive: a handle the audio
     * thread can name must outlive any model reading that has lost its slot.
     * Splitting the two would make that depend on the caller publishing exactly
     * what it was handed.
     *
     * Nothing is swapped when the slots have not moved. A publish waits for the
     * block the callback is in, and a clip dragged across the timeline
     * republishes its snapshot at gesture speed with the same slots every time;
     * a table that says what the live one says is a wait for nothing.
     *
     * What comes back is what is live, for a caller that wants to look at it.
     */
    std::shared_ptr<const LaunchHandleTable> publishHandles(const ClipSnapshot& clips,
                                                            LaunchHandleFeed& feed);

    /// The handle for one slot, made on first ask. Unlike a device or a tap
    /// there is no factory to decline: a handle is the engine's own state
    /// rather than something the host builds, so a slot the model names always
    /// has one.
    LaunchHandle& handle(const SlotKey& key);

    /// The handle for one slot, or null if nothing has asked for it yet.
    LaunchHandle* findHandle(const SlotKey& key) const;

    /// Objects currently owned, for tests and diagnostics.
    std::size_t size() const;

  private:
    /// The tap for one Meter op, asking the factory once if there is none. A
    /// factory that declines is asked again next time, exactly as it is for
    /// everything else: a mixer that opens after the plan was published should
    /// get its meters at the next publish rather than never.
    LevelTap* realiseMeter(const OpKey& key);

    RuntimeStateFactory& factory_;

    /// What everything the store holds has been prepared for. Set by the first
    /// realise() and only ever changed by one that disagrees with it.
    RenderContext context_;
    bool hasContext_ = false;

    std::unordered_map<DeviceKey, std::unique_ptr<EngineDevice>, DeviceKeyHash> devices_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> clipAudio_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> clipMidi_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> sessionAudio_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> sessionMidi_;

    /// One per slot rather than per track, because a slot's state has to
    /// survive another slot playing in between: launch scene 0, then scene 1,
    /// then scene 0 again, and the first slot's loop phase and played range are
    /// still its own. Retired by the same rule as everything else here, which
    /// is the model no longer naming the slot (#2301).
    std::map<SlotKey, std::unique_ptr<LaunchHandle>> handles_;

    /// The table the caller was last given to publish, which is what the audio
    /// thread can name. Held so eviction has the keep set that cannot be
    /// waived, the way the live plan is for everything else here.
    std::shared_ptr<const LaunchHandleTable> publishedHandles_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> audioInputs_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> midiInputs_;

    /// Keyed by the op's whole key, and retained by the model IDs that key
    /// names: a meter is kept while the track or device it reads exists, which
    /// is the same rule everything else here follows read through the one
    /// binding whose identity is the whole op rather than a DeviceKey or TrackId.
    std::map<OpKey, std::unique_ptr<LevelTap>> meters_;

    /// The values the host has asked to read back, kept across publishes for
    /// the reason everything else here is: an edit elsewhere in the project
    /// must not restate a number something is watching. Retained by the model
    /// IDs its key names, and by the live table, which is the only thing that
    /// says whether the audio thread can reach one.
    std::map<ParamKey, std::unique_ptr<ValueTap>> valueTaps_;
};

}  // namespace magda::engine
