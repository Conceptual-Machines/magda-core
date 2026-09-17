#include "PlaybackPositionTimer.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioEngine.hpp"
#include "core/ClipManager.hpp"
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

    // Drain audio-thread session clip state events before querying playhead
    engine_.processSessionStateEvents();

    bool isPlaying = engine_.isPlaying();

    // Establish the per-clip half of this tick before dispatching any timeline event. Timeline
    // listeners update synchronous UI such as the ruler, while grids may repaint later; both must
    // observe the same Session positions rather than opposite sides of this timer callback.
    auto clipPositions =
        isPlaying ? engine_.getActiveClipPlayheadPositions() : std::unordered_map<ClipId, double>{};
    if (!clipPositions.empty()) {
        auto& cm = ClipManager::getInstance();
        for (const auto& [clipId, pos] : clipPositions) {
            if (auto* clip = cm.getClip(clipId))
                clip->sessionPlayheadPos = pos;
        }
    }

    // Detect engine play/stop transitions that happened outside the UI
    // (e.g. SessionClipScheduler starting transport for clip playback)
    bool isRecording = engine_.isRecording();
    const bool playStateChanged = isPlaying != wasPlaying_;
    const bool recordStateChanged = isRecording != wasRecording_;
    if (playStateChanged || recordStateChanged) {
        timeline_.dispatch(SetPlaybackStateEvent{isPlaying, isRecording});
        if (playStateChanged && onPlayStateChanged)
            onPlayStateChanged(isPlaying);
        if (recordStateChanged && onRecordStateChanged)
            onRecordStateChanged(isRecording);
        wasPlaying_ = isPlaying;
        wasRecording_ = isRecording;
    }

    if (isPlaying) {
        double transportPos = engine_.getCurrentPosition();
        timeline_.dispatch(SetPlaybackPositionEvent{transportPos});

        if (!clipPositions.empty() && onSessionPlayheadUpdate)
            onSessionPlayheadUpdate(clipPositions);
    }

    // CPU usage + xrun update (throttled)
    if (onCpuUsageUpdate && ++cpuUpdateCounter_ >= CPU_UPDATE_TICKS) {
        cpuUpdateCounter_ = 0;
        auto* dm = engine_.getDeviceManager();
        if (dm) {
            juce::String deviceName;
            double sampleRate = 0.0;
            int bufferSize = 0;
            if (auto* device = dm->getCurrentAudioDevice()) {
                deviceName = device->getName();
                sampleRate = device->getCurrentSampleRate();
                bufferSize = device->getCurrentBufferSizeSamples();
            }
            onCpuUsageUpdate(static_cast<float>(dm->getCpuUsage()), dm->getXRunCount(), deviceName,
                             sampleRate, bufferSize);
        }
    }
}

}  // namespace magda
