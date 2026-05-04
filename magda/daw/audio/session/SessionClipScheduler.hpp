#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <unordered_map>
#include <unordered_set>

#include "../../core/ClipManager.hpp"

namespace magda {

namespace te = tracktion;

// Forward declarations
class AudioBridge;
class SessionClipAudioMonitor;

/**
 * @brief Thin command layer over TE's native ClipSlot/LaunchHandle system.
 *
 * This is NOT a parallel scheduler — TE owns playback state via LaunchHandle.
 * This class:
 *  - Sends play/stop commands to TE (launchHandle->play()/stop())
 *  - Maintains user intent via TrackInfo::activeSessionClipId
 *  - Syncs TrackPlaybackMode / playSlotClips
 *  - Reads LaunchHandle state for UI queries
 *  - Reads audio-thread playhead positions for UI display
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

    /** Query the play state of a session clip from TE's LaunchHandle. */
    SessionClipPlayState getClipPlayState(ClipId clipId) const;

    /** Stop all active session clips, clear activeSessionClipId, revert to arrangement. */
    void deactivateAllSessionClips();

    /** Schedule a quantized stop for the active session clip on a track.
        Used by scene launch for empty slots (empty slot = stop that track). */
    void stopSessionTrack(TrackId trackId);

    /** True while a quantized stop is in flight on this track — between
        `stopSessionTrack` being called and the LaunchHandle actually
        reporting Stopped. Used by the UI to blink the stop affordance. */
    bool isSessionTrackStopPending(TrackId trackId) const;

    /** Re-launch any session clips that have activeSessionClipId set but aren't
        currently playing. Call synchronously when transport starts to avoid
        the 33ms polling delay. */
    void relaunchActiveClips();

    /** Returns true if any track has an activeSessionClipId set. */
    bool hasActiveClips() const;

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
     * @brief Poll LaunchHandle state and update UI.
     * Must be called periodically from the message thread (e.g. from PlaybackPositionTimer).
     */
    void processStateEvents();

  private:
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

    /** Derive and sync TrackPlaybackMode for all tracks from activeSessionClipId.
        Tracks with an active session clip → Session, others → Arrangement. */
    void syncTrackPlaybackModes();

    /** Ensure a LaunchHandle shared_ptr is held for a clip (keeps it alive for audio thread). */
    void retainLaunchHandle(ClipId clipId);

    /** Release the LaunchHandle shared_ptr for a clip.
     *
     *  Two-phase: the handle is parked in `deferredHandlesCurrent_` and only
     *  destroyed in a subsequent `processStateEvents()` once the audio thread
     *  has had at least one full block to drain pending Unmonitor commands.
     *  Without the delay, a queue-overflow drop or a not-yet-processed command
     *  would leave the audio thread holding a raw pointer to a freed handle.
     */
    void releaseLaunchHandle(ClipId clipId);

    /** Bulk variant of releaseLaunchHandle — moves every entry out of
     *  activeLaunchHandles_ into the deferred-release queue. */
    void releaseAllLaunchHandles();

    /** Drain handles parked one cycle ago. Called from processStateEvents()
     *  AFTER the audio thread has had an opportunity to see queued Unmonitor
     *  commands; the shared_ptr destructors run here on the message thread. */
    void drainDeferredHandles();

    /** Set up audio-thread monitoring for a clip's playhead position. */
    void sendMonitorCommand(ClipId clipId);

    /** Remove audio-thread monitoring for a clip. */
    void sendUnmonitorCommand(ClipId clipId);

    AudioBridge& audioBridge_;
    te::Edit& edit_;
    SessionClipAudioMonitor& audioMonitor_;

    // Per-clip timing data for playhead beat→second conversion
    std::unordered_map<ClipId, ClipLaunchData> clipLaunchData_;

    // The "primary" playhead clip — used by getSessionPlayheadPosition()
    ClipId playheadClipId_ = INVALID_CLIP_ID;

    // LaunchHandle shared_ptrs kept alive while clips are active.
    std::unordered_map<ClipId, std::shared_ptr<te::LaunchHandle>> activeLaunchHandles_;

    // Two-phase release of LaunchHandles. When releaseLaunchHandle() is
    // called we move the shared_ptr into `deferredHandlesCurrent_` rather
    // than dropping it. Each processStateEvents() pass first runs the
    // destructors of the previous cycle (`deferredHandlesPrevious_`) — the
    // audio thread has already had a full block to drain Unmonitor
    // commands, so the raw pointer it held is no longer in use — and then
    // rotates current → previous. Costs one extra ~33ms of memory liveness
    // per stopped clip; in exchange the audio thread can never see a raw
    // pointer to a freed te::LaunchHandle, even if the command queue
    // dropped the Unmonitor.
    std::vector<std::shared_ptr<te::LaunchHandle>> deferredHandlesCurrent_;
    std::vector<std::shared_ptr<te::LaunchHandle>> deferredHandlesPrevious_;

    // Tracks last-notified play state per clip to avoid redundant UI notifications
    std::unordered_map<ClipId, SessionClipPlayState> lastNotifiedState_;

    // Tracks with a quantized stop in flight (set by stopSessionTrack, cleared
    // when the orphaned LaunchHandle actually reaches Stopped). Drives the
    // empty-slot stop-icon blink.
    std::unordered_set<TrackId> stopPendingTracks_;

    // Snapshot of state at the start of a launch batch.
    // Prevents sequential clipPlaybackRequested calls within the same batch
    // (e.g. scene launch) from seeing each other's mutations.
    struct LaunchSnapshot {
        bool transportPlaying = false;
        bool hasActiveClips = false;
    };
    LaunchSnapshot launchSnapshot_;
    bool launchSnapshotValid_ = false;

    // Transport state tracking for stop/resume detection
    bool wasTransportPlaying_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipScheduler)
};

}  // namespace magda
