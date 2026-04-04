#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <unordered_set>

#include "../core/ClipManager.hpp"

namespace magda {

namespace te = tracktion;

// Forward declarations
class AudioBridge;
class SessionClipAudioMonitor;

/**
 * @brief Schedules session clip playback using Tracktion Engine's native ClipSlot/LaunchHandle
 * system.
 *
 * Uses TE's built-in clip launcher:
 * - ClipSlot hosts clips for launching (no dynamic timeline creation)
 * - LaunchHandle provides lock-free play/stop (no graph rebuilds)
 * - SlotControlNode renders slot clips with its own local playhead
 *
 * State transitions are detected by SessionClipAudioMonitor on the audio thread
 * and communicated via a lock-free state queue. The scheduler processes these
 * events on the message thread via processStateEvents().
 *
 * All public methods run on the message thread.
 */
class SessionClipScheduler : public ClipManagerListener {
  public:
    SessionClipScheduler(AudioBridge& audioBridge, te::Edit& edit,
                         SessionClipAudioMonitor& audioMonitor);
    ~SessionClipScheduler() override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) override;

    /** Query the play state of a session clip. */
    SessionClipPlayState getClipPlayState(ClipId clipId) const;

    /** Stop all active session clips and clear state. */
    void deactivateAllSessionClips();

    /** Returns true if any session clips are currently active (playing or queued). */
    bool hasActiveClips() const {
        return !activeClips_.empty();
    }

    /** Returns the looped session playhead position (seconds), or -1.0 if no session clips active.
        This tracks the most recently launched clip (used by clip editors). */
    double getSessionPlayheadPosition() const;

    /** Returns the clip ID that the session playhead currently tracks, or INVALID_CLIP_ID. */
    ClipId getSessionPlayheadClipId() const {
        return playheadClipId_;
    }

    /** Returns per-clip playhead positions for all active clips. */
    std::unordered_map<ClipId, double> getActiveClipPlayheadPositions() const;

    /**
     * @brief Drain audio-thread state events and update UI state.
     * Must be called periodically from the message thread (e.g. from PlaybackPositionTimer).
     */
    void processStateEvents();

  private:
    /** Push a Monitor command to the audio thread for a clip. */
    void sendMonitorCommand(ClipId clipId);

    /** Push an Unmonitor command to the audio thread for a clip. */
    void sendUnmonitorCommand(ClipId clipId);

    /** Convert elapsed beats (from audio monitor) to looped seconds for a clip. */
    double elapsedBeatsToSeconds(ClipId clipId, double elapsedBeats) const;

    // Per-clip timing data for playhead conversion (beats → seconds)
    struct ClipLaunchData {
        double loopLengthBeats = 0.0;
        double clipLengthBeats = 0.0;
        bool looping = false;
    };

    /** Update ClipLaunchData from current clip properties. */
    void updateLaunchTimings(ClipId clipId, const ClipInfo* clip);

    AudioBridge& audioBridge_;
    te::Edit& edit_;
    SessionClipAudioMonitor& audioMonitor_;

    // Clips we've activated via LaunchHandle (playing or queued).
    std::unordered_set<ClipId> activeClips_;

    // Per-clip timing data for playhead beat→second conversion
    std::unordered_map<ClipId, ClipLaunchData> clipLaunchData_;

    // Tracks the audio-thread-reported play state per clip
    std::unordered_map<ClipId, SessionClipPlayState> lastNotifiedState_;

    // The "primary" playhead clip — used by getSessionPlayheadPosition()
    ClipId playheadClipId_ = INVALID_CLIP_ID;

    // LaunchHandle shared_ptrs kept alive while clips are monitored on audio thread.
    // Released only after Unmonitor command is sent (audio thread stops reading the handle).
    std::unordered_map<ClipId, std::shared_ptr<te::LaunchHandle>> activeLaunchHandles_;

    // Snapshot of state at the start of a launch batch.
    // Prevents sequential clipPlaybackRequested calls within the same batch
    // (e.g. scene launch) from seeing each other's mutations.
    struct LaunchSnapshot {
        bool transportPlaying = false;
        bool hasActiveClips = false;
    };
    LaunchSnapshot launchSnapshot_;
    bool launchSnapshotValid_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipScheduler)
};

}  // namespace magda
