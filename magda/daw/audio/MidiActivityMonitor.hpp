#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

#include "../core/TypeIds.hpp"

namespace magda {

/**
 * @brief Lock-free per-track MIDI activity tracking using monotonic counters.
 *
 * Each note-on increments a per-track counter. The UI compares the current
 * counter to its last-seen value to detect new activity — no notes are lost
 * regardless of polling rate.
 *
 * Thread Safety:
 * - Write: MIDI thread (triggerActivity) — lock-free atomic increment
 * - Read: UI thread (getActivityCounter) — lock-free atomic load
 */
class MidiActivityMonitor {
  public:
    MidiActivityMonitor() = default;
    ~MidiActivityMonitor() = default;

    /**
     * @brief Trigger MIDI activity for a track (MIDI thread safe)
     * @param trackId The track that received MIDI
     */
    void triggerActivity(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;
        activityCounters_[trackId].fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief Get the current activity counter for a track (UI thread)
     * @param trackId The track to check
     * @return Current counter value. Compare with previously stored value to detect new activity.
     */
    uint32_t getActivityCounter(TrackId trackId) const {
        if (trackId < 0 || trackId >= kMaxTracks)
            return 0;
        return activityCounters_[trackId].load(std::memory_order_acquire);
    }

    /**
     * @brief Clear all counters. Call only when audio is stopped.
     */
    void clearAll() {
        for (auto& counter : activityCounters_)
            counter.store(0, std::memory_order_relaxed);
    }

    static constexpr int getMaxTracks() {
        return kMaxTracks;
    }

  private:
    static constexpr int kMaxTracks = 512;
    std::array<std::atomic<uint32_t>, kMaxTracks> activityCounters_{};
};

}  // namespace magda
