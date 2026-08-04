#pragma once

#include <memory>
#include <unordered_map>

#include "exec/PlanBindings.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file RuntimeStateStore.hpp
 * @brief Who owns the objects a plan renders through, and for how long.
 *
 * A plan is topology and nothing else, so it cannot own a plugin instance or a
 * file reader: it is rebuilt every time a device moves, and rebuilding an
 * instrument because a fader was reordered above it is the rebuild-click
 * problem in miniature. The store owns them instead, keyed by model ID the way
 * OpKey is, so the same edit that recompiles the plan leaves the objects it
 * names untouched.
 *
 * Everything here runs off the audio thread.
 */

namespace magda::engine {

/**
 * @brief Makes the runtime objects a plan asks for.
 *
 * Implemented by the host, because the engine has no idea what a device is: it
 * knows a DeviceId, and the host knows which plugin that is. Returning nullptr
 * is allowed and means the object does not exist; the executor reports the
 * op as unbound rather than pretending otherwise.
 */
class RuntimeStateFactory {
  public:
    virtual ~RuntimeStateFactory() = default;

    virtual std::unique_ptr<EngineDevice> createDevice(DeviceId) {
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
};

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
     */
    PlanBindings realise(const RenderPlan& plan);

    /**
     * @brief Drop everything @p live does not name, and say how much went.
     *
     * Call only after the swap has published @p live. Before that, the objects
     * being dropped are still reachable from the plan on the audio thread, and
     * this is where their destructors run.
     */
    std::size_t retireUnused(const RenderPlan& live);

    /// Objects currently owned, for tests and diagnostics.
    std::size_t size() const;

  private:
    RuntimeStateFactory& factory_;

    std::unordered_map<DeviceId, std::unique_ptr<EngineDevice>> devices_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> clipAudio_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> clipMidi_;
    std::unordered_map<TrackId, std::unique_ptr<EngineAudioSource>> audioInputs_;
    std::unordered_map<TrackId, std::unique_ptr<EngineMidiSource>> midiInputs_;
};

}  // namespace magda::engine
