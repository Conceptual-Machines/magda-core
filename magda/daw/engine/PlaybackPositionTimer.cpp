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

    const bool isPlaying = engine_.isPlaying();

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
    const bool isRecording = engine_.isRecording();
    const bool playStateChanged = isPlaying != wasPlaying_;
    const bool recordStateChanged = isRecording != wasRecording_;
    const auto& timelinePlayhead = timeline_.getState().playhead;
    const bool modelWasPlaying = timelinePlayhead.isPlaying;
    const bool modelWasRecording = timelinePlayhead.isRecording;
    const bool punchArmed = timeline_.isPunchArmed() && !engine_.hasSampleAccuratePunch();

    // Record requests update the timeline optimistically. The native host may
    // reject one synchronously when no armed MIDI input can record, leaving the
    // engine false without an engine transition for the old edge-only polling
    // to notice. Reconcile that mismatch on the next tick. A punch-in waiting
    // for its boundary deliberately shows Record before the engine records, so
    // it is the one mismatch that must survive.
    const bool recordModelMismatch = !punchArmed && modelWasRecording != isRecording;
    if (playStateChanged || recordStateChanged || recordModelMismatch) {
        // Ordinary Play remains edge-driven because starting the host can be
        // asynchronous. A Record mismatch reconciles both flags because a
        // record request can also own the optimistic Play state.
        const bool reconciledRecording = punchArmed && isPlaying ? modelWasRecording : isRecording;
        timeline_.dispatch(SetPlaybackStateEvent{isPlaying, reconciledRecording});
        const bool playModelChanged = modelWasPlaying != isPlaying;
        const bool recordModelChanged = modelWasRecording != reconciledRecording;
        if ((playStateChanged || playModelChanged) && onPlayStateChanged)
            onPlayStateChanged(isPlaying);
        if ((recordStateChanged || recordModelChanged) && onRecordStateChanged)
            onRecordStateChanged(reconciledRecording);
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
