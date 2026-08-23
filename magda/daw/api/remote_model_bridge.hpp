#pragma once

#include <memory>

namespace magda {

class TransportApi;

namespace remote {

class RemoteApiService;

/**
 * @brief Feeds MAGDA's model listeners into a `RemoteApiService`'s change source.
 *
 * Without this, a remote subscriber only learns about changes that a remote
 * client itself made — the user moving a fader in the MAGDA UI would be
 * invisible. That is the primary case for OSC feedback (#1757 §4) and half the
 * point of WebSocket subscriptions (#1857), so the bridge belongs with the
 * change source rather than inside either adapter.
 *
 * Attaches to the `TrackManager`, `ClipManager`, `AutomationManager`,
 * `SelectionManager`, and `ProjectManager` singletons for its lifetime and
 * detaches on destruction. Construct and destroy on the message thread, which
 * is where those managers notify.
 *
 * Every callback does the same O(1) work — mark a topic dirty — so a
 * high-frequency source costs one atomic per notification and is collapsed to
 * one delivery per flush by `ChangeSource`.
 *
 * The implementation lives in `remote_model_bridge_live.cpp` and is the only
 * part of the remote API layer that touches model singletons, following the
 * same `_live` split as the rest of the facade.
 */
class ModelChangeBridge {
  public:
    explicit ModelChangeBridge(RemoteApiService& service, TransportApi* transport = nullptr);
    ~ModelChangeBridge();

    ModelChangeBridge(const ModelChangeBridge&) = delete;
    ModelChangeBridge& operator=(const ModelChangeBridge&) = delete;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace remote
}  // namespace magda
