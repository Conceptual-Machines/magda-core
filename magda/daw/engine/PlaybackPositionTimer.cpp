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
        double transportPos = engine_.getCurrentPosition();
        timeline_.dispatch(SetPlaybackPositionEvent{transportPos});

        // Write per-clip playhead positions into ClipInfo and notify UI
        auto clipPositions = engine_.getActiveClipPlayheadPositions();
        if (!clipPositions.empty()) {
            auto& cm = ClipManager::getInstance();
            for (const auto& [clipId, pos] : clipPositions) {
                if (auto* clip = cm.getClip(clipId))
                    clip->sessionPlayheadPos = pos;
            }

            if (onSessionPlayheadUpdate)
                onSessionPlayheadUpdate(clipPositions);
        }
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
