#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>

#include "../../core/ClipTypes.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Command sent from the message thread to the audio thread
 * to start or stop monitoring a session clip's LaunchHandle state.
 *
 * Lifetime contract for Monitor commands:
 *  - launchHandle points to a te::LaunchHandle that the message thread
 *    holds via shared_ptr in SessionClipScheduler::activeLaunchHandles_.
 *  - When the message thread queues an Unmonitor command, it does NOT
 *    immediately drop the shared_ptr. Instead, the handle is parked in a
 *    deferred-release queue and only dropped one processStateEvents() cycle
 *    later. By that point the audio thread has had multiple blocks to drain
 *    pending Unmonitor commands.
 *  - This double-buffering means the audio thread can hold the raw pointer
 *    safely even if a queued Unmonitor was dropped because the queue was
 *    full — the handle is still alive for at least one cycle after the
 *    Unmonitor was queued.
 */
struct SessionClipCommand {
    ClipId clipId = INVALID_CLIP_ID;

    enum class Action : uint8_t { Monitor, Unmonitor };
    Action action = Action::Monitor;

    // Raw pointer — lifetime managed by message thread (see class doc)
    te::LaunchHandle* launchHandle = nullptr;
};

/**
 * @brief Lock-free SPSC queue for message-thread-to-audio-thread session clip commands.
 *
 * Message thread pushes Monitor/Unmonitor commands.
 * Audio thread pops and updates its monitored clip set.
 */
class SessionClipCommandQueue {
  public:
    static constexpr int kQueueSize = 64;

    SessionClipCommandQueue() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

    bool push(const SessionClipCommand& cmd) {
        int writeIdx = writeIndex_.load(std::memory_order_relaxed);
        int readIdx = readIndex_.load(std::memory_order_acquire);

        int nextWrite = (writeIdx + 1) & (kQueueSize - 1);
        if (nextWrite == readIdx) {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        buffer_[writeIdx] = cmd;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }

    /// Total number of pushes that were dropped because the queue was full.
    /// Reads/clears are eventually consistent — drained periodically from
    /// the message thread to surface lost Monitor/Unmonitor commands that
    /// would otherwise disappear silently.
    uint32_t getDroppedCount() const {
        return droppedCount_.load(std::memory_order_relaxed);
    }
    void clearDroppedCount() {
        droppedCount_.store(0, std::memory_order_relaxed);
    }

    bool pop(SessionClipCommand& cmd) {
        int writeIdx = writeIndex_.load(std::memory_order_acquire);
        int readIdx = readIndex_.load(std::memory_order_relaxed);

        if (readIdx == writeIdx)
            return false;

        cmd = buffer_[readIdx];
        readIndex_.store((readIdx + 1) & (kQueueSize - 1), std::memory_order_release);
        return true;
    }

    void clear() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
        droppedCount_.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<SessionClipCommand, kQueueSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
    std::atomic<uint32_t> droppedCount_{0};
};

}  // namespace magda
