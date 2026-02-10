#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "../core/TypeIds.hpp"

namespace magda {

/**
 * @brief Lock-free per-track MIDI sidechain trigger bus.
 *
 * Written on the audio thread (by MidiSidechainMonitorPlugin) when MIDI note-on/off
 * events are detected. Read on the message thread (by updateAllMods) to detect new
 * sidechain triggers without scanning clips.
 *
 * Each note-on/off increments a monotonic counter. The consumer compares the current
 * counter to its last-seen value to detect new events — no events are lost regardless
 * of polling rate or loop boundaries.
 *
 * Thread Safety:
 * - Write: Audio thread (triggerNoteOn/Off) — lock-free atomic increment
 * - Read: Message thread (getNoteOnCounter/getNoteOffCounter) — lock-free atomic load
 */
class SidechainTriggerBus {
  public:
    static SidechainTriggerBus& getInstance() {
        static SidechainTriggerBus instance;
        return instance;
    }

    /**
     * @brief Trigger a note-on for a track (audio thread safe)
     * @param trackId The source track that received MIDI note-on
     */
    void triggerNoteOn(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;
        tracks_[trackId].noteOnCounter.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief Trigger a note-off for a track (audio thread safe)
     * @param trackId The source track that received MIDI note-off
     */
    void triggerNoteOff(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;
        tracks_[trackId].noteOffCounter.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief Get the current note-on counter for a track (message thread)
     * @param trackId The track to check
     * @return Current counter value. Compare with previously stored value to detect new events.
     */
    uint64_t getNoteOnCounter(TrackId trackId) const {
        if (trackId < 0 || trackId >= kMaxTracks)
            return 0;
        return tracks_[trackId].noteOnCounter.load(std::memory_order_acquire);
    }

    /**
     * @brief Get the current note-off counter for a track (message thread)
     * @param trackId The track to check
     * @return Current counter value. Compare with previously stored value to detect new events.
     */
    uint64_t getNoteOffCounter(TrackId trackId) const {
        if (trackId < 0 || trackId >= kMaxTracks)
            return 0;
        return tracks_[trackId].noteOffCounter.load(std::memory_order_acquire);
    }

    /**
     * @brief Clear all counters. Call only when audio is stopped.
     */
    void clearAll() {
        for (auto& track : tracks_) {
            track.noteOnCounter.store(0, std::memory_order_relaxed);
            track.noteOffCounter.store(0, std::memory_order_relaxed);
        }
    }

  private:
    SidechainTriggerBus() = default;

    static constexpr int kMaxTracks = 512;

    struct TrackTriggerState {
        std::atomic<uint64_t> noteOnCounter{0};
        std::atomic<uint64_t> noteOffCounter{0};
    };

    std::array<TrackTriggerState, kMaxTracks> tracks_;
};

}  // namespace magda
