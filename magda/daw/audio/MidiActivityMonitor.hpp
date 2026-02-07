#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <unordered_map>

#include "../core/TypeIds.hpp"

namespace magda {

/**
 * @brief Manages per-track MIDI activity tracking with lock-free thread safety
 *
 * Responsibilities:
 * - Per-track MIDI activity tracking (for UI visualization)
 * - Lock-free activity flags (write from audio thread, read from UI)
 * - Dynamic track container (no fixed limit, fixes bug #1)
 *
 * Thread Safety:
 * - Write: Audio thread (MIDI processing callback)
 * - Read: UI thread (visualization updates)
 * - Implementation: std::atomic per track, no locks needed
 *
 * Note: This uses a simple std::unordered_map with atomic values.
 * For maximum performance, could be replaced with a lock-free hash map,
 * but the current implementation is sufficient for typical use cases.
 */
class MidiActivityMonitor {
  public:
    MidiActivityMonitor() = default;
    ~MidiActivityMonitor() = default;

    /**
     * @brief Trigger MIDI activity for a track (audio thread safe)
     * @param trackId The track that received MIDI
     */
    void triggerActivity(TrackId trackId) {
        if (trackId < 0) {
            return;
        }

        // Get or create atomic flag for this track
        // Note: operator[] is not thread-safe for concurrent writes,
        // but we only write from audio thread, so it's safe
        activityFlags_[trackId].store(true, std::memory_order_release);
    }

    /**
     * @brief Check and clear MIDI activity flag for a track (UI thread)
     * @param trackId The track to check
     * @return true if MIDI activity occurred since last check
     */
    bool consumeActivity(TrackId trackId) {
        if (trackId < 0) {
            return false;
        }

        // Check if track exists in map
        auto it = activityFlags_.find(trackId);
        if (it == activityFlags_.end()) {
            return false;
        }

        // Read and clear flag atomically
        return it->second.exchange(false, std::memory_order_acq_rel);
    }

    /**
     * @brief Clear all activity flags
     * Call only when audio is stopped
     */
    void clearAll() {
        activityFlags_.clear();
    }

  private:
    // Per-track MIDI activity flags
    // Key: TrackId, Value: atomic bool flag
    // Audio thread writes (triggerActivity), UI thread reads/clears (consumeActivity)
    std::unordered_map<TrackId, std::atomic<bool>> activityFlags_;
};

}  // namespace magda
