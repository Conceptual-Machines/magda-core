#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <vector>

#include "../core/ClipManager.hpp"

namespace magda {

namespace te = tracktion;

// Forward declarations
class AudioBridge;

/**
 * @brief Schedules session clip playback by creating/removing Tracktion Engine clips dynamically.
 *
 * Listens to ClipManager for playback state changes (trigger/stop), manages quantized launch
 * timing, and creates/removes TE clips at the correct transport position.
 *
 * Flow:
 *   User clicks clip slot
 *     -> ClipManager::triggerClip() sets isQueued=true
 *       -> notifyClipPlaybackStateChanged()
 *         -> SessionClipScheduler::clipPlaybackStateChanged()
 *           -> if LaunchQuantize::None: immediately create TE clip
 *           -> else: queue for next beat/bar boundary (timer checks)
 *
 * All operations run on the message thread (ClipManager notifications + juce::Timer).
 */
class SessionClipScheduler : public ClipManagerListener, private juce::Timer {
  public:
    SessionClipScheduler(AudioBridge& audioBridge, te::Edit& edit);
    ~SessionClipScheduler() override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPlaybackStateChanged(ClipId clipId) override;

  private:
    void timerCallback() override;

    // Quantization helpers
    double getCurrentBeatPosition() const;
    double getNextQuantizeBoundary(LaunchQuantize q, double currentBeat) const;

    // Clip lifecycle
    void activateSessionClip(ClipId clipId);
    void deactivateSessionClip(ClipId clipId);
    void deactivateAllSessionClips();

    // Queued clip tracking
    struct QueuedClip {
        ClipId clipId;
        double targetStartBeat;
    };
    std::vector<QueuedClip> queuedClips_;

    // Active session clip tracking (clipId -> TE engine clip ID)
    std::map<ClipId, std::string> activeSessionClips_;

    AudioBridge& audioBridge_;
    te::Edit& edit_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionClipScheduler)
};

}  // namespace magda
