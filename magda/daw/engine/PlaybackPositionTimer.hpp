#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <unordered_map>

#include "../core/ClipTypes.hpp"

namespace magda {

class AudioEngine;
class TimelineController;

/**
 * @brief Timer that polls the audio engine for playhead position updates
 *
 * This class periodically polls the AudioEngine for the current
 * playback position and dispatches SetPlaybackPositionEvent to the
 * TimelineController, which then notifies all listeners.
 */
class PlaybackPositionTimer : private juce::Timer {
  public:
    PlaybackPositionTimer(AudioEngine& engine, TimelineController& timeline);
    ~PlaybackPositionTimer() override;

    void start();
    void stop();
    bool isRunning() const;

    /** Callback fired on the message thread when play state changes. */
    std::function<void(bool)> onPlayStateChanged;

    /** Callback fired each tick with per-clip playhead positions (clip ID → seconds).
        Only called when at least one session clip has an active playhead. */
    std::function<void(const std::unordered_map<ClipId, double>&)> onSessionPlayheadUpdate;

    /** Callback fired periodically with CPU usage (0.0 to 1.0) and xrun count. */
    std::function<void(float, int)> onCpuUsageUpdate;

  private:
    void timerCallback() override;

    AudioEngine& engine_;
    TimelineController& timeline_;

    bool wasPlaying_ = false;    // Track engine playing state for change detection
    bool wasRecording_ = false;  // Track engine recording state for change detection
    int cpuUpdateCounter_ = 0;   // Throttle CPU updates

    static constexpr int UPDATE_INTERVAL_MS = 30;  // ~33fps for smooth playhead
    static constexpr int CPU_UPDATE_TICKS = 15;    // Every ~500ms
};

}  // namespace magda
