#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "PumpDevice.hpp"

namespace magda {
class AudioEngine;
}

/**
 * @file EngineRig.hpp
 * @brief One of the app's two engines, headless, on the pump device (#2082).
 *
 * Built through createDefaultAudioEngine and opened through its own AudioIOControl, so what
 * the bench measures is what the app runs. The readiness and latency questions are the only
 * engine-specific code, and each is answered from the engine's own state.
 */

namespace magda::parity {

class EngineRig {
  public:
    /// "native" or "tracktion". Null with @p failure set when the engine will not come up.
    static std::unique_ptr<EngineRig> create(const std::string& engine, double sampleRate,
                                             int blockSize, std::string& failure);
    ~EngineRig();

    EngineRig(const EngineRig&) = delete;
    EngineRig& operator=(const EngineRig&) = delete;

    AudioEngine& engine() {
        return *engine_;
    }

    /// The backend the pump is registered under. Owned by the engine's device manager.
    PumpBackend& backend() const {
        return *backend_;
    }

    /// The device, once the manager has opened it.
    PumpDevice* device() const {
        return backend_->device();
    }

    /// Called just before a project is committed, so readiness waits for the engine to hear of it.
    void loadStarting();

    /// Whether the engine renders everything the model holds: the plan or graph built from it,
    /// every external plugin loaded and every proxy rendered. On the message thread.
    bool isReady();

    /// What the engine says its output graph delays the timeline by. On the message thread.
    int reportedLatencySamples() const;

  private:
    EngineRig() = default;

    std::unique_ptr<AudioEngine> engine_;
    PumpBackend* backend_ = nullptr;
    bool native_ = false;
    std::optional<std::uint64_t> publishRequestsAtLoad_;
    bool graphBuiltForLoad_ = false;
};

}  // namespace magda::parity
