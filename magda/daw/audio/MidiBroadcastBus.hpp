#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>

#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Lock-free per-track MIDI message double buffer for cross-track MIDI routing.
 *
 * Written on the audio thread by SidechainMonitorPlugin when MIDI events are detected
 * on a source track. Read on the audio thread by MidiReceivePlugin on destination tracks
 * to inject actual MIDI messages into the plugin chain.
 *
 * Double-buffered: source writes to the write buffer, destination reads from the read
 * buffer (previous block's messages). Swap happens at endBlock(). This introduces one
 * audio-block latency (typically < 5ms at 256 samples / 48kHz).
 *
 * Thread Safety:
 * - Write: Audio thread (beginBlock/addMessage/endBlock) — one writer per TrackId
 * - Read: Audio thread (getMessages) — multiple readers OK (reads from stable read buffer)
 * - No locks — uses atomic index swap for double buffering
 */
class MidiBroadcastBus {
  public:
    static MidiBroadcastBus& getInstance() {
        static MidiBroadcastBus instance;
        return instance;
    }

    /**
     * @brief Begin a new block for a source track — clears the write buffer.
     * Call at the start of SidechainMonitorPlugin::applyToBuffer().
     */
    void beginBlock(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;
        auto& slot = tracks_[trackId];
        auto writeIdx = 1 - slot.readIndex.load(std::memory_order_acquire);
        slot.buffers[writeIdx].clear();
    }

    /**
     * @brief Add a MIDI message to the write buffer for a source track.
     * Call for each message in the current block.
     */
    void addMessage(TrackId trackId, const te::MidiMessageWithSource& msg) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;
        auto& slot = tracks_[trackId];
        auto writeIdx = 1 - slot.readIndex.load(std::memory_order_acquire);
        auto& buf = slot.buffers[writeIdx];
        if (buf.size() < kMaxMessagesPerBlock)
            buf.add(msg);
    }

    /**
     * @brief End the block — swap read/write buffers so destinations see this block's messages.
     * Call at the end of SidechainMonitorPlugin::applyToBuffer().
     */
    void endBlock(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;
        auto& slot = tracks_[trackId];
        auto newReadIdx = 1 - slot.readIndex.load(std::memory_order_acquire);
        slot.readIndex.store(newReadIdx, std::memory_order_release);
    }

    /**
     * @brief Get the read buffer (previous block's messages) for a source track.
     * Call from MidiReceivePlugin::applyToBuffer() to read MIDI from a source.
     */
    const te::MidiMessageArray& getMessages(TrackId trackId) const {
        if (trackId < 0 || trackId >= kMaxTracks) {
            static const te::MidiMessageArray empty;
            return empty;
        }
        const auto& slot = tracks_[trackId];
        auto readIdx = slot.readIndex.load(std::memory_order_acquire);
        return slot.buffers[readIdx];
    }

    /**
     * @brief Clear all buffers. Call only when audio is stopped.
     */
    void clearAll() {
        for (auto& slot : tracks_) {
            slot.buffers[0].clear();
            slot.buffers[1].clear();
            slot.readIndex.store(0, std::memory_order_relaxed);
        }
    }

  private:
    MidiBroadcastBus() {
        // Pre-allocate buffer capacity to avoid audio-thread heap allocation
        for (auto& slot : tracks_) {
            slot.buffers[0].reserve(kMaxMessagesPerBlock);
            slot.buffers[1].reserve(kMaxMessagesPerBlock);
        }
    }

    static constexpr int kMaxTracks = 512;
    static constexpr int kMaxMessagesPerBlock = 128;

    struct TrackSlot {
        te::MidiMessageArray buffers[2];
        std::atomic<int> readIndex{0};
    };

    std::array<TrackSlot, kMaxTracks> tracks_;
};

}  // namespace magda
