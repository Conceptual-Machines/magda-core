#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

#include "../core/TypeIds.hpp"

namespace magda {

/**
 * @brief Manages per-track MIDI activity tracking with lock-free thread safety
 *
 * Responsibilities:
 * - Per-track MIDI activity tracking (for UI visualization)
 * - Lock-free activity flags (write from audio thread, read from UI)
 * - Fixed-size array (512 tracks max, safe for audio thread)
 *
 * Thread Safety:
 * - Write: Audio thread (MIDI processing callback)
 * - Read: UI thread (visualization updates)
 * - Implementation: std::array of std::atomic<bool>, no locks, no allocations
 *
 * Design Note:
 * Uses fixed-size array instead of std::unordered_map to avoid allocations
 * and rehashing on the audio thread (real-time safety violation).
 * 512 tracks should be sufficient for any reasonable project.
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
        if (trackId < 0 || trackId >= kMaxTracks) {
            // Out of bounds - log warning but don't crash
            // This is rare and non-critical, so we just skip it
            return;
        }

        activityFlags_[trackId].store(true, std::memory_order_release);
    }

    /**
     * @brief Check and clear MIDI activity flag for a track (UI thread)
     * @param trackId The track to check
     * @return true if MIDI activity occurred since last check
     */
    bool consumeActivity(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks) {
            return false;
        }

        // Read and clear flag atomically
        return activityFlags_[trackId].exchange(false, std::memory_order_acq_rel);
    }

    /**
     * @brief Clear all activity flags
     * Call only when audio is stopped
     */
    void clearAll() {
        for (auto& flag : activityFlags_) {
            flag.store(false, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Get maximum number of tracks supported
     */
    static constexpr int getMaxTracks() {
        return kMaxTracks;
    }

  private:
    // Maximum number of tracks we support
    // 512 should be more than enough for any reasonable project
    // (increased from original 128 to avoid the bug in issue #1)
    static constexpr int kMaxTracks = 512;

    // Per-track MIDI activity flags
    // Audio thread writes (triggerActivity), UI thread reads/clears (consumeActivity)
    // Using std::array for lock-free, allocation-free access (real-time safe)
    std::array<std::atomic<bool>, kMaxTracks> activityFlags_;
};

}  // namespace magda
