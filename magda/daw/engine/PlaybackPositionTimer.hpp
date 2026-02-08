#pragma once

#include <juce_events/juce_events.h>

#include <functional>

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

    /** Callback fired each tick with the looped clip-local position (seconds).
        Only called when session clips are active. -1.0 means no session clips. */
    std::function<void(double)> onSessionPlayheadUpdate;

  private:
    void timerCallback() override;

    AudioEngine& engine_;
    TimelineController& timeline_;

    bool wasPlaying_ = false;    // Track engine playing state for change detection
    bool wasRecording_ = false;  // Track engine recording state for change detection

    static constexpr int UPDATE_INTERVAL_MS = 30;  // ~33fps for smooth playhead
};

}  // namespace magda
