#include "PlaybackPositionTimer.hpp"

#include "tracktion_engine_wrapper.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineEvents.hpp"

namespace magica {

PlaybackPositionTimer::PlaybackPositionTimer(TracktionEngineWrapper& engine,
                                             TimelineController& timeline)
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
    if (engine_.isPlaying()) {
        double position = engine_.getCurrentPosition();
        // Only update playback position (the moving cursor), not edit position
        timeline_.dispatch(SetPlaybackPositionEvent{position});
    }
}

}  // namespace magica
