#include "PlaybackPositionTimer.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

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
    bool isRecording = engine_.isRecording();
    if (isPlaying != wasPlaying_ || isRecording != wasRecording_) {
        timeline_.dispatch(SetPlaybackStateEvent{isPlaying, isRecording});
        if (onPlayStateChanged)
            onPlayStateChanged(isPlaying);
        wasPlaying_ = isPlaying;
        wasRecording_ = isRecording;
    }

    if (isPlaying) {
        double sessionPos = engine_.getSessionPlayheadPosition();
        auto sessionClipId = engine_.getSessionPlayheadClipId();
        double transportPos = engine_.getCurrentPosition();

        // Always use the real transport position for the main timeline playhead.
        // The session position is passed through so clip editors (waveform,
        // piano roll) can show a looped playhead independent of the arrangement.
        timeline_.dispatch(SetPlaybackPositionEvent{transportPos, sessionPos, sessionClipId});

        // Session clip playhead callback (for per-clip progress bars)
        if (onSessionPlayheadUpdate) {
            onSessionPlayheadUpdate(sessionPos);
        }
    }

    // CPU usage update (throttled)
    if (onCpuUsageUpdate && ++cpuUpdateCounter_ >= CPU_UPDATE_TICKS) {
        cpuUpdateCounter_ = 0;
        auto* dm = engine_.getDeviceManager();
        if (dm)
            onCpuUsageUpdate(static_cast<float>(dm->getCpuUsage()));
    }
}

}  // namespace magda
