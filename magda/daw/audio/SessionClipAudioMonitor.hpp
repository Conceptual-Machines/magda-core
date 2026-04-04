#pragma once

#include <array>
#include <atomic>
#include <unordered_map>

#include "SessionClipCommandQueue.hpp"
#include "SessionClipStateQueue.hpp"

namespace magda {

/**
 * @brief Monitors session clip LaunchHandle state on the audio thread.
 *
 * Called each audio buffer from SessionMonitorPlugin::applyToBuffer().
 * Detects playing/stopped transitions and pushes events to the state queue.
 * Writes per-clip playhead positions (elapsed beats) to atomic slots for
 * low-latency UI reads.
 *
 * Thread safety:
 * - process() runs on audio thread only
 * - commandQueue() is written by message thread, read by audio thread
 * - stateQueue() is written by audio thread, read by message thread
 * - playhead slots: written by audio thread, read by message thread
 */
class SessionClipAudioMonitor {
  public:
    static constexpr int kMaxMonitoredClips = 64;

    SessionClipAudioMonitor() = default;

    /**
     * @brief Called from the audio thread each buffer.
     * Drains command queue, monitors LaunchHandle state, pushes events.
     * @param transportPositionSeconds Current transport position in seconds
     */
    void process(double transportPositionSeconds);

    /** Message-thread accessor: push Monitor/Unmonitor commands. */
    SessionClipCommandQueue& commandQueue() {
        return commandQueue_;
    }

    /** Message-thread accessor: pop state transition events. */
    SessionClipStateQueue& stateQueue() {
        return stateQueue_;
    }

    /**
     * @brief Read the latest playhead position for a clip (elapsed beats, unlooped).
     * Returns -1.0 if the clip is not being monitored or not playing.
     * Safe to call from any thread.
     */
    double getClipElapsedBeats(ClipId clipId) const;

    /**
     * @brief Get all active clip playhead positions (clipId → elapsed beats).
     * Safe to call from any thread.
     */
    void getActivePlayheadBeats(std::unordered_map<ClipId, double>& out) const;

    /**
     * @brief Clear all monitored clips. Call only when audio is stopped.
     */
    void clear();

  private:
    struct MonitoredClip {
        ClipId clipId = INVALID_CLIP_ID;
        te::LaunchHandle* launchHandle = nullptr;
        te::LaunchHandle::PlayState lastState = te::LaunchHandle::PlayState::stopped;
    };

    // Fixed-size array of monitored clips (no allocations on audio thread)
    std::array<MonitoredClip, kMaxMonitoredClips> monitoredClips_{};
    int numMonitored_ = 0;

    // Per-clip playhead: elapsed beats (monotonic, unlooped) from getPlayedRange()
    struct PlayheadSlot {
        std::atomic<ClipId> clipId{INVALID_CLIP_ID};
        std::atomic<double> elapsedBeats{-1.0};
    };
    std::array<PlayheadSlot, kMaxMonitoredClips> playheadSlots_{};

    SessionClipCommandQueue commandQueue_;
    SessionClipStateQueue stateQueue_;

    // Find index of a clip in monitoredClips_, or -1
    int findClipIndex(ClipId clipId) const;
    // Find index in playheadSlots_ for a clip, or allocate one. Returns -1 if full.
    int findOrAllocPlayheadSlot(ClipId clipId);
    // Free a playhead slot
    void freePlayheadSlot(ClipId clipId);
};

}  // namespace magda
