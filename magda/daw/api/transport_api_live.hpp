#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "transport_api.hpp"

namespace tracktion {
inline namespace engine {
class Edit;
}
}  // namespace tracktion

namespace magda {

/**
 * Live TransportApi — talks to the current Edit's TransportControl.
 *
 * The Edit is reached via an injected getter callback (set in
 * MagdaApiLive::setEditAccessor) because the wrapper's currentEdit_ can
 * be replaced when projects open / close. When the getter returns null
 * (headless tests, no project), reads return safe defaults and writes
 * are no-ops.
 */
class TransportApiLive : public TransportApi {
  public:
    using EditGetter = std::function<tracktion::Edit*()>;
    using TransportFn = std::function<void()>;

    TransportApiLive();
    ~TransportApiLive() override;

    void setEditGetter(EditGetter g);

    /** Route play() through this callback when set, instead of going
     *  straight to Tracktion's transport. The application wires this to
     *  TimelineController::dispatch(StartPlaybackEvent) so script-driven
     *  play uses the same playhead-aware path as the on-screen Play
     *  button. With no callback set, falls back to a direct
     *  transport.play(false). */
    void setPlayDispatcher(TransportFn fn) {
        playDispatch_ = std::move(fn);
    }

    /** Same idea for stop — wired to StopPlaybackEvent dispatch. */
    void setStopDispatcher(TransportFn fn) {
        stopDispatch_ = std::move(fn);
    }

    /** Same idea for loop — wired to TimelineController::dispatch(SetLoopEnabledEvent)
     *  so API/script toggles use controller semantics without UI-only selection promotion. */
    void setLoopDispatcher(std::function<void(bool)> fn) {
        loopDispatch_ = std::move(fn);
    }

    void play() override;
    void stop() override;
    void setRecording(bool recording) override;
    bool isPlaying() const override;
    bool isRecording() const override;
    bool isLoopEnabled() const override;
    void setLoopEnabled(bool enabled) override;
    double getPositionBeats() const override;
    void setPositionBeats(double beats) override;
    double beatsAtBarOffset(double beats, int deltaBars) const override;
    int addStateListener(StateListener listener) override;
    void removeStateListener(int token) override;
    void refreshStateSource() override;

  private:
    class StateObserver;
    struct ListenerEntry {
        int token = 0;
        StateListener callback;
    };

    tracktion::Edit* edit() const {
        return getEdit_ ? getEdit_() : nullptr;
    }

    void notifyStateListeners();

    EditGetter getEdit_;
    TransportFn playDispatch_;
    TransportFn stopDispatch_;
    std::function<void(bool)> loopDispatch_;
    std::vector<ListenerEntry> stateListeners_;
    std::unique_ptr<StateObserver> stateObserver_;
    int nextStateListenerToken_ = 1;
};

}  // namespace magda
