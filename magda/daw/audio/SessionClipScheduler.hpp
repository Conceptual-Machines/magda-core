#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <unordered_set>

#include "../core/ClipManager.hpp"

namespace magda {

namespace te = tracktion;

// Forward declarations
class AudioBridge;

/**
 * @brief Schedules session clip playback using Tracktion Engine's native ClipSlot/LaunchHandle
 * system.
 *
 * Uses TE's built-in clip launcher:
 * - ClipSlot hosts clips for launching (no dynamic timeline creation)
 * - LaunchHandle provides lock-free play/stop (no graph rebuilds)
 * - SlotControlNode renders slot clips with its own local playhead
 *
 * Play state is derived directly from the LaunchHandle (single source of truth).
 * The scheduler only tracks which clips have been activated (activeClips_) for
 * lifecycle management (transport start/stop, timer monitoring).
 *
 * All operations run on the message thread.
 */
class SessionClipScheduler : public ClipManagerListener, private juce::Timer {
  public:
    SessionClipScheduler(AudioBridge& audioBridge, te::Edit& edit);
    ~SessionClipScheduler() override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) override;

    /** Query the play state of a session clip, derived from the TE LaunchHandle. */
    SessionClipPlayState getClipPlayState(ClipId clipId) const;

    /** Stop all active session clips and clear state. */
    void deactivateAllSessionClips();

    /** Returns true if any session clips are currently active (playing or queued). */
    bool hasActiveClips() const {
        return !activeClips_.empty();
    }

    /** Returns the looped session playhead position (seconds), or -1.0 if no session clips active.
     */
    double getSessionPlayheadPosition() const;

    /** Returns the clip ID that the session playhead currently tracks, or INVALID_CLIP_ID. */
    ClipId getSessionPlayheadClipId() const {
        return playheadClipId_;
    }

  private:
    void timerCallback() override;

    /** Cache wall-clock durations for playhead tracking from clip state. */
    void updateLaunchTimings(const ClipInfo* clip);

    /** Query the LaunchHandle state for a clip. Returns Stopped if clip/handle not found. */
    SessionClipPlayState queryLaunchHandleState(ClipId clipId) const;

    AudioBridge& audioBridge_;
    te::Edit& edit_;

    // Clips we've activated via LaunchHandle (playing or queued).
    // Actual Queued/Playing/Stopped state is derived from the LaunchHandle.
    std::unordered_set<ClipId> activeClips_;

    // Transport position at which the first session clip was launched
    double launchTransportPos_ = 0.0;
    // Loop length in seconds (for playhead wrapping when looping)
    double launchLoopLength_ = 0.0;
    // Full clip duration in seconds (for playhead when not looping)
    double launchClipLength_ = 0.0;
    // Whether the primary launched clip is looping
    bool launchClipLooping_ = false;
    // Clip awaiting Playing transition — used to correct launchTransportPos_
    // for quantized launches (playhead starts from actual play moment, not click)
    ClipId pendingPlayheadClip_ = INVALID_CLIP_ID;
    // The clip that the current playhead position tracks
    ClipId playheadClipId_ = INVALID_CLIP_ID;

    // Debounce counter for stopped detection.
    // LaunchHandle::getQueuedStatus() uses try_to_lock which can fail when
    // the audio thread holds the spin mutex, causing a queued clip to appear
    // "Stopped" for a single timer tick. Require multiple consecutive Stopped
    // readings before actually removing a clip.
    static constexpr int kStoppedThreshold = 3;  // ~100ms at 30Hz
    std::unordered_map<ClipId, int> stoppedCounters_;

    // Cache last-notified state per clip so we only fire notifications on actual transitions
    std::unordered_map<ClipId, SessionClipPlayState> lastNotifiedState_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipScheduler)
};

}  // namespace magda
