#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <set>

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
 * Flow:
 *   User clicks clip slot
 *     -> ClipManager::triggerClip() sets isQueued=true
 *       -> notifyClipPlaybackStateChanged()
 *         -> SessionClipScheduler::clipPlaybackStateChanged()
 *           -> Ensure transport playing
 *           -> LaunchHandle::play() (lock-free atomic)
 *           -> ClipManager::setClipPlayingState(true)
 *
 *   User stops clip
 *     -> ClipManager::stopClip() sets both false
 *       -> clipPlaybackStateChanged()
 *         -> LaunchHandle::stop() (lock-free atomic)
 *
 *   One-shot clip ends naturally
 *     -> Timer detects LaunchHandle::getPlayingStatus() == stopped
 *       -> ClipManager::setClipPlayingState(false)
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
    void clipPlaybackStateChanged(ClipId clipId) override;

    /** Stop all launched session clips and clear state. */
    void deactivateAllSessionClips();

    /** Returns true if any session clips are currently launched. */
    bool hasLaunchedClips() const {
        return !launchedClips_.empty();
    }

    /** Returns the looped session playhead position (seconds), or -1.0 if no session clips active.
     */
    double getSessionPlayheadPosition() const;

  private:
    void timerCallback() override;

    AudioBridge& audioBridge_;
    te::Edit& edit_;

    // Clips we've launched via LaunchHandle (to detect natural end of one-shot clips)
    std::set<ClipId> launchedClips_;

    // Transport position at which the first session clip was launched
    double launchTransportPos_ = 0.0;
    // Duration of the primary launched clip (seconds) for playhead looping
    double launchClipLength_ = 0.0;
    // Whether the primary launched clip is looping
    bool launchClipLooping_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipScheduler)
};

}  // namespace magda
