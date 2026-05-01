#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

namespace magda {

/**
 * @brief A MIDI event entry for the debug monitor queue
 *
 * Captures essential MIDI event data for display in the MIDI monitor.
 * Designed to be small and trivially copyable for lock-free queue use.
 */
struct MidiEventEntry {
    juce::String deviceName;
    int channel = 0;
    enum Type { NoteOn, NoteOff, CC, PitchBend, Other } type = Other;
    int data1 = 0;           // note number or CC number
    int data2 = 0;           // velocity or CC value
    int pitchBendValue = 0;  // 0-16383, center=8192
    double timestamp = 0.0;
};

/**
 * @brief Lock-free SPSC queue for MIDI events (audio thread → UI thread)
 *
 * Audio thread pushes MIDI events, UI thread pops and displays them.
 * Uses a fixed-size ring buffer for predictable memory behavior.
 * Global queue (not per-track) so the monitor shows ALL MIDI regardless of routing.
 */
class MidiEventQueue {
  public:
    static constexpr int kQueueSize = 256;  // Power of 2 for fast modulo

    MidiEventQueue() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Push a MIDI event (called from audio/MIDI callback thread)
     * @param entry The MIDI event to queue
     * @return true if successfully queued, false if queue full
     */
    bool push(const MidiEventEntry& entry) {
        int writeIdx = writeIndex_.load(std::memory_order_relaxed);
        int readIdx = readIndex_.load(std::memory_order_acquire);

        int nextWrite = (writeIdx + 1) & (kQueueSize - 1);
        if (nextWrite == readIdx) {
            return false;  // Queue full - drop event
        }

        buffer_[writeIdx] = entry;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop a MIDI event (called from UI thread)
     * @param entry Output parameter for the event
     * @return true if event was available, false if queue empty
     */
    bool pop(MidiEventEntry& entry) {
        int writeIdx = writeIndex_.load(std::memory_order_acquire);
        int readIdx = readIndex_.load(std::memory_order_relaxed);

        if (readIdx == writeIdx) {
            return false;  // Queue empty
        }

        entry = buffer_[readIdx];
        readIndex_.store((readIdx + 1) & (kQueueSize - 1), std::memory_order_release);
        return true;
    }

    /**
     * @brief Check if queue has pending events
     */
    bool hasPending() const {
        return writeIndex_.load(std::memory_order_acquire) !=
               readIndex_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Clear all pending events (call only when safe)
     */
    void clear() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<MidiEventEntry, kQueueSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
};

}  // namespace magda
