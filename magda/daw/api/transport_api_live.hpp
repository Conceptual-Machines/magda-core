#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "transport_api.hpp"

namespace magda {

/**
 * Live transport facade over engine-owned callbacks.
 *
 * Reads return safe defaults and writes are no-ops when their callbacks are
 * unset. The native engine supplies state and notifications; application dispatchers route
 * transport commands through TimelineController when a window is attached.
 */
class TransportApiLive : public TransportApi {
  public:
    using TransportFn = std::function<void()>;

    TransportApiLive();
    ~TransportApiLive() override;

    /** State and meter queries owned by the current engine. */
    struct EngineState {
        std::function<bool()> playing;
        std::function<bool()> recording;
        std::function<bool()> looping;
        std::function<double()> positionBeats;
        std::function<double()> beatsPerBar;
    };
    void setEngineState(EngineState state) {
        engineState_ = std::move(state);
    }

    /** Seek through TimelineController so the next play starts where the seek put it. */
    void setSeekDispatcher(std::function<void(double)> fn) {
        seekDispatch_ = std::move(fn);
    }

    /** Record through TimelineController, which arms, punches, and starts playback. */
    void setRecordDispatcher(std::function<void(bool)> fn) {
        recordDispatch_ = std::move(fn);
    }

    /** Report an engine-owned play, stop, record, or loop change. */
    void notifyStateChanged() {
        notifyStateListeners();
    }

    /** Route play through the application controller, or an engine-owned dispatcher. */
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

  private:
    struct ListenerEntry {
        int token = 0;
        StateListener callback;
    };

    void notifyStateListeners();

    TransportFn playDispatch_;
    TransportFn stopDispatch_;
    std::function<void(bool)> loopDispatch_;
    std::function<void(double)> seekDispatch_;
    std::function<void(bool)> recordDispatch_;
    EngineState engineState_;
    std::vector<ListenerEntry> stateListeners_;
    int nextStateListenerToken_ = 1;
};

}  // namespace magda
