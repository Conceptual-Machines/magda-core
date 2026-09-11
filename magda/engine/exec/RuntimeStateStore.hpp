#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "clip/ClipSnapshot.hpp"
#include "exec/PlanBindings.hpp"
#include "io/RecordingFeed.hpp"
#include "launch/LaunchHandle.hpp"
#include "launch/SessionLauncher.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/LevelTap.hpp"
#include "tap/RecordTap.hpp"
#include "tap/ValueTap.hpp"

namespace magda {
struct TrackInfo;
}

/**
 * @file RuntimeStateStore.hpp
 * @brief Who owns the objects a plan renders through, and for how long.
 *
 * A plan is topology and nothing else, so it cannot own a plugin instance or
 * a file reader: it's rebuilt every time a device moves, and rebuilding an
 * instrument because a fader was reordered above it would be the
 * rebuild-click problem in miniature. The store owns them instead, keyed by
 * section-aware model identity the way OpKey is, so the same edit that
 * recompiles the plan leaves the objects it names untouched.
 *
 * A take is here for the same reason, with more at stake (#2465). An instrument
 * rebuilt mid-note clicks; a take rebuilt mid-pass is a split file or a hole.
 * Nothing can recompute one either: it is a file handle, a write position, and
 * a queue holding samples nobody has written yet.
 *
 * Everything here runs off the audio thread.
 */

namespace magda::engine {

struct ParamTable;

/**
 * @brief Makes the runtime objects a plan asks for.
 *
 * Implemented by the host, since the engine has no idea what a device is: it
 * knows a section-aware DeviceKey, and the host knows which plugin that is.
 * Returning nullptr means the object doesn't exist; the executor reports the
 * op as unbound rather than pretending otherwise.
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
     * @brief Keys whose device is no longer the one the store holds (#2572).
     *
     * Asked once per publish, before anything is realised. The instance being
     * replaced stays alive until the swap, since the live plan still names it.
     */
    virtual std::set<DeviceKey> devicesToRebuild() {
        return {};
    }

    /**
     * @brief The level tap behind one Meter op, or nullptr for one nobody reads.
     *
     * Declining is the ordinary answer, not a failure: a plan has a meter at
     * every track, every device slot and the master, and a host with no
     * mixer on screen wants none of them. Declining costs only the meter --
     * the op still renders, since a Meter op is a point in the signal path
     * first and a display second.
     *
     * Handed the whole key because a device's meter and the device itself
     * share a DeviceId; which track/device and what kind of meter are both
     * read off the key.
     */
    virtual std::unique_ptr<LevelTap> createMeter(const OpKey&) {
        return nullptr;
    }

    /**
     * @brief The values the host wants read back (#2122).
     *
     * Asked once per plan publish, off the audio thread. Every key returned
     * gets a ValueTap readable at whatever rate the host draws: a
     * parameter's key for the position it resolved to, a modifier's for the
     * output it published.
     *
     * A modifier is named by its key with `index = -1`, not the default a
     * hand-built ParamKey carries -- index 0 is the modifier's own Rate
     * parameter, so a default-index key would ask about the rate rather
     * than the output. modifierKeyFor() builds the right shape from a
     * ControlTarget so callers don't have to choose.
     *
     * Shaped as one question rather than a createValueTap() beside the
     * others because of scale: a plan has hundreds of ops and a host with a
     * mixer open wants meters on a good fraction of them, so asking op by
     * op is cheap. A parameter table has thousands of entries and a host
     * wants a few dozen, so asking one by one would mean thousands of
     * declines to find them -- the host states what it wants instead.
     *
     * The store builds the taps itself, rather than handing back instances,
     * because a ValueTap needs no host help to construct; the methods above
     * exist only because the engine can't build a device or file reader on
     * its own.
     *
     * Returning nothing is the ordinary answer: an offline render, or a
     * session with no windows open, wants none of these.
     */
    virtual std::vector<ParamKey> valuesToTap() {
        return {};
    }
};

/**
 * @brief Model IDs that still exist, whatever the current plan happens to use.
 *
 * Eviction is model-aware rather than plan-aware: bypass and chain power are
 * structural, so a bypassed device contributes no ops at all. If eviction
 * were plan-keyed, switching chain power off would tear down every plugin on
 * the track and switching it back on would rebuild them, losing tails,
 * plugin state and load time on a gesture the user expects to be free.
 * Plan-named means playing, model-named means kept; only deletion from the
 * model destroys anything.
 */
struct RuntimeStateIds {
    std::set<DeviceKey> devices;
    std::set<TrackId> tracks;

    /// Takes the model still says may run: a track armed, with an input of
    /// that material (#2465). Alone here in naming what is allowed to continue
    /// rather than what exists, because disarming ends a take without changing
    /// any topology the plan could be asked about.
    ///
    /// A slot take is retained by the same arm, being the same track's input.
    /// ClipLane::recordSlots says which slot a take goes into and not whether
    /// one may run, so a host that arms a slot without arming its track has a
    /// recording the next edit ends.
    std::set<TakeKey> takes;
};

// Launch handles are deliberately not tracked here: a slot is a clip, so
// what exists is answered by the snapshot, and their lifetime follows
// publishHandles() instead (#2301).

/**
 * @brief Every device and track the model holds, nested racks included.
 *
 * Deliberately not the compiler's walk: that one answers what plays, this
 * one answers what exists, and the gap between the two is exactly what gets
 * kept.
 */
RuntimeStateIds collectRuntimeStateIds(const std::vector<TrackInfo>& tracks,
                                       const TrackInfo& master);

/**
 * @brief The runtime objects behind a plan's leaf ops, owned across swaps.
 */
class RuntimeStateStore {
  public:
    explicit RuntimeStateStore(RuntimeStateFactory& factory) : factory_(factory) {}

    /**
     * @brief Bindings for @p plan, creating what is new and reusing what is not.
     *
     * Never destroys anything: a plan being prepared isn't published yet, so
     * the audio thread is still rendering the previous one.
     *
     * Preparing an object happens here, as part of creation, and nowhere
     * else: a device the store already holds may be inside the live plan's
     * process() this instant, and prepare() on a real plugin resizes and
     * clears what process() is reading -- a race, except on the runs it
     * costs the tail, delay line and filter state the swap exists to keep.
     * So an object is prepared exactly once, when new, before anything can
     * reach it.
     *
     * @p context changing is not a live operation: every held object is
     * prepared again for it, safe only because a sample rate or block size
     * change means the audio device has stopped and there's no callback to
     * race.
     */
    PlanBindings realise(const RenderPlan& plan, const RenderContext& context);

    /**
     * @brief Destroy what neither @p livePlan nor @p modelIds names.
     *
     * Two keep sets for two different reasons, and the plan's is the one
     * that can't be waived: anything @p livePlan names is reachable from the
     * audio thread right now, so destroying it is a use-after-free, not an
     * early eviction. Taking the plan here rather than trusting the
     * caller's IDs is what guards against them disagreeing -- and they
     * won't always agree, since plans compile from a model revision and
     * publish later, so a caller collecting IDs from the current model can
     * hand over a set that's already lost something the plan still uses.
     *
     * @p modelIds only ever extends retention, to what exists but isn't
     * playing: a bypassed device, a chain with the power off, an unarmed
     * track's input source.
     *
     * Call only after the swap, with the plan that is now live. Destructors
     * run on the calling thread.
     *
     * @p liveTable is the parameter table that plan renders with, and is to
     * the value taps what the plan is to everything else: the one thing
     * that says which taps the audio thread can reach (a plan doesn't name
     * a parameter, so nothing else here could answer it). Null means a
     * session rendering without a table, where no value tap is reachable.
     *
     * Required rather than defaulted, for the same reason the plan is
     * required rather than trusted to the caller's IDs: omitting it would
     * leave eviction to the model IDs alone, and a tap the live table
     * carries but the ID set doesn't name would be destroyed while the
     * executor still holds its pointer -- the next block writes through
     * freed memory, on the audio thread.
     */
    std::size_t releaseDeleted(const RenderPlan& livePlan, const RuntimeStateIds& modelIds,
                               const ParamTable* liveTable);

    /**
     * @brief The tap publishing @p key, or nullptr (#2122).
     *
     * The other half of valuesToTap(): the host says which values it wants
     * read back, and this is where it collects them. Off the audio thread;
     * what it hands back is readable from any thread, the whole point of it.
     *
     * Null until a publish has been attempted, since that's when taps are
     * made -- a window opening between two publishes draws a value nothing
     * has offered yet, and gets one at the next publish rather than never.
     * "Attempted" rather than "succeeded" because taps are realised before
     * a plan is known to prepare, so non-null says the store has one, not
     * that anything is publishing through it; the tap's own count answers
     * that (zero means nothing in the engine moves this value, and the
     * model's own value is the answer -- see ValueTap.hpp).
     *
     * Good until the next publish, like every other pointer the store hands
     * out: a tap whose device was deleted goes away when the plan that
     * named it stops being live, so a caller holding one across that has a
     * pointer to nothing, and asking again is how it finds out.
     */
    ValueTap* valueTap(const ParamKey& key) const;

    /**
     * @brief The tap the Meter op at @p key writes to, or nullptr (#2570).
     *
     * Null for a meter the factory declined and for an op no plan has named.
     * One reader, since LevelTap::read is destructive.
     */
    LevelTap* meterTap(const OpKey& key) const;

    /**
     * @brief Make a handle for every slot @p clips names, publish them, and
     *        retire the ones the snapshot has stopped naming.
     *
     * On the publishing thread, and the whole of a handle's lifetime --
     * nothing else creates or retires them.
     *
     * All three happen in one call because they can't be separated: a clip
     * edit doesn't compile a plan, so releaseDeleted() wouldn't run between
     * a slot being emptied and refilled, and the refilled slot would come
     * up already playing. Retiring is only safe on the far side of the
     * publish, which waits for the current callback's block.
     *
     * Nothing is swapped or retired when the slots haven't moved, so a drag
     * republishing the snapshot at gesture speed with the same slots is
     * free.
     *
     * @p requests is told the incarnations this publish assigns, so a launch
     * asked for against a handle that has gone is dropped rather than applied
     * to the one that replaced it (#2305).
     *
     * @p retired takes an end for every run that was sounding on a handle this
     * publish drops. No block can report one -- the handle is gone before the
     * next -- and a capture with no end for a run cannot say what played
     * (#2464). Null for a caller that does not capture.
     */
    std::shared_ptr<const LaunchHandleTable> publishHandles(
        const ClipSnapshot& clips, LaunchHandleFeed& feed, LaunchRequestQueue& requests,
        std::vector<SlotRunEvent>* retired = nullptr);

    /// @brief The handle for @p key, or null when no published snapshot names it.
    LaunchHandle* findHandle(const SlotKey& key) const;

    /**
     * @brief The tap take @p key publishes its pass to, made if there is none.
     *
     * On the publishing thread, and before the take, since the recorder is
     * built against it. @p settings is ignored for a tap that already exists:
     * resizing its arrays would allocate underneath a reader.
     */
    RecordTap& realiseTakeTap(const TakeKey& key, const RecordTapSettings& settings);

    /// @brief The tap for @p key, or null. On the publishing thread.
    const RecordTap* takeTap(const TakeKey& key) const;

    /// @brief Own @p take as @p key's, until something ends it.
    ///
    /// On the publishing thread, and only for a key nothing is recording: a
    /// take the callback can still reach has to go through @ref releaseTake
    /// before anything may destroy it.
    void holdTake(const TakeKey& key, std::unique_ptr<TakeCapture> take);

    /// @brief Every take being fed, in key order. What the callback's set is
    ///        built from, on the publishing thread.
    RecordingTakes liveTakes() const;

    /// @brief Takes @p modelIds has stopped naming: the arm switched off, the
    ///        input taken away, the track deleted (#2465).
    ///
    /// Named here and released separately, because a take has to leave the
    /// callback's set before anything may close it.
    std::vector<TakeKey> unnamedTakes(const RuntimeStateIds& modelIds) const;

    /// A take on its way out, with the tap it writes to.
    struct ReleasedTake {
        std::unique_ptr<TakeCapture> take;
        std::unique_ptr<RecordTap> tap;
    };

    /**
     * @brief Hand @p key's take over, empty if there is none.
     *
     * On the publishing thread, and only once the callback can no longer reach
     * it. The tap goes with it: finish() writes to it, and an overlay drawn
     * from it has to stand until the clip appears.
     */
    ReleasedTake releaseTake(const TakeKey& key);

    /**
     * @brief Where @p key's state is published, or null (#2303).
     *
     * Made and retired with the handle beside it, and on the publishing thread
     * both to look up and to read: publishHandles() destroys a dropped slot's
     * tap as soon as the audio thread is out of it, and waits for nothing else.
     */
    const LaunchTap* launchTap(const SlotKey& key) const;

    /**
     * @brief A lease on the device at @p key, or nothing (#2581).
     *
     * What lets a host reach a device for something that is not a block: the
     * lease carries the instance rather than pointing at it, so a publish that
     * evicts it partway through a state read does not take it away. On the
     * publishing thread, which is the only thread this map is written from.
     */
    std::shared_ptr<EngineDevice> device(DeviceKey key) const;

    /// @brief Objects currently owned, for tests and diagnostics.
    std::size_t size() const;

  private:
    /// The tap for one Meter op, asking the factory once if there is none. A
    /// factory that declines is asked again next time, so a mixer opened
    /// after the plan was published still gets its meters at the next
    /// publish.
    LevelTap* realiseMeter(const OpKey& key);

    RuntimeStateFactory& factory_;

    /// What everything the store holds has been prepared for. Set by the
    /// first realise() and only ever changed by one that disagrees with it.
    RenderContext context_;
    bool hasContext_ = false;

    /// Shared rather than owned outright, so that a control operation can hold
    /// the device it is reading across a publish that drops it (#2581). The
    /// store is still the only thing that creates or evicts one.
    std::unordered_map<DeviceKey, std::shared_ptr<EngineDevice>, DeviceKeyHash> devices_;

    /// An all-notes-off each device is owed, outliving every plan the way the
    /// instance does (PlanBindings::deviceMidiPanic). Never evicted: it is a
    /// byte per device key the project has ever realised, and a flag that
    /// outlives its instance panics a fresh plugin, which is holding nothing.
    std::unordered_map<DeviceKey, std::unique_ptr<std::atomic<char>>, DeviceKeyHash> devicePanic_;

    /// Instances a rebuild took out of devices_, held until releaseDeleted()
    /// (#2572).
    std::vector<std::shared_ptr<EngineDevice>> retired_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> clipAudio_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> clipMidi_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> sessionAudio_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> sessionMidi_;

    /// A slot's handle and which handle it is, since a request made against the
    /// clip that was here has the same key as one made against its replacement.
    struct Slot {
        std::unique_ptr<LaunchHandle> handle;
        std::uint64_t incarnation = 0;

        /// Beside the handle, so a host's pointer survives a publish that did
        /// not retire the slot.
        std::unique_ptr<LaunchTap> tap;
    };

    /// One per slot rather than per track: a slot's loop phase and played
    /// range have to survive another slot playing in between. Made and
    /// retired by publishHandles() alone (#2301).
    std::map<SlotKey, Slot> handles_;

    /// Never reused, so an incarnation names one handle for the life of the
    /// session.
    std::uint64_t nextIncarnation_ = 0;

    /// The takes being fed, and the taps they publish through. Two maps
    /// because they do not begin together: the tap is made first, since the
    /// recorder is built against it (#2465).
    std::map<TakeKey, std::unique_ptr<TakeCapture>> takes_;
    std::map<TakeKey, std::unique_ptr<RecordTap>> takeTaps_;

    /// What was last published, so a publish that changes nothing is skipped.
    std::shared_ptr<const LaunchHandleTable> publishedHandles_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> audioInputs_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> midiInputs_;

    /// Keyed by the op's whole key and retained by the model IDs that key
    /// names -- a meter is kept while the track or device it reads exists,
    /// the same rule everything else here follows, read through the one
    /// binding whose identity is the whole op rather than a DeviceKey or
    /// TrackId.
    std::map<OpKey, std::unique_ptr<LevelTap>> meters_;

    /// The values the host has asked to read back, kept across publishes so
    /// an edit elsewhere in the project doesn't restate a number something
    /// is watching. Retained by the model IDs its key names, and by the
    /// live table, the only thing that says whether the audio thread can
    /// reach one.
    std::map<ParamKey, std::unique_ptr<ValueTap>> valueTaps_;
};

}  // namespace magda::engine
