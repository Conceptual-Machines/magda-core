#include "PlaybackPositionTimer.hpp"

#include "AudioEngine.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineEvents.hpp"

namespace magda {

PlaybackPositionTimer::PlaybackPositionTimer(AudioEngine& engine, TimelineController& timeline)
    : engine_(engine), timeline_(timeline) {}

PlaybackPositionTimer::~PlaybackPositionTimer() {
    stopTimer();
}

void PlaybackPositionTimer::start() {
    startTimer(UPDATE_INTERVAL_MS);
}

void PlaybackPositionTimer::stop() {
    stopTimer();
}

bool PlaybackPositionTimer::isRunning() const {
    return isTimerRunning();
}

void PlaybackPositionTimer::timerCallback() {
    // Update trigger state for transport-synced devices (tone generator, etc.)
    engine_.updateTriggerState();

    bool isPlaying = engine_.isPlaying();

    // Detect engine play/stop transitions that happened outside the UI
    // (e.g. SessionClipScheduler starting transport for clip playback)
    if (isPlaying != wasPlaying_) {
        timeline_.dispatch(SetPlaybackStateEvent{isPlaying});
        if (onPlayStateChanged)
            onPlayStateChanged(isPlaying);
        wasPlaying_ = isPlaying;
    }

    if (isPlaying) {
        double sessionPos = engine_.getSessionPlayheadPosition();

        // When session clips are active, loop the editor playhead too
        double position = (sessionPos >= 0.0) ? sessionPos : engine_.getCurrentPosition();
        timeline_.dispatch(SetPlaybackPositionEvent{position});

        // Session clip playhead callback (for per-clip progress bars)
        if (onSessionPlayheadUpdate) {
            onSessionPlayheadUpdate(sessionPos);
        }
    }
}

}  // namespace magda
