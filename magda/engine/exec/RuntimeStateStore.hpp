#pragma once

#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "exec/PlanBindings.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/LevelTap.hpp"

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
     */
    std::size_t releaseDeleted(const RenderPlan& livePlan, const RuntimeStateIds& modelIds);

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
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> audioInputs_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> midiInputs_;

    /// Keyed by the op's whole key, and retained by the model IDs that key
    /// names: a meter is kept while the track or device it reads exists, which
    /// is the same rule everything else here follows read through the one
    /// binding whose identity is the whole op rather than a DeviceKey or TrackId.
    std::map<OpKey, std::unique_ptr<LevelTap>> meters_;
};

}  // namespace magda::engine
