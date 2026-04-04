#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>

#include "../core/ClipTypes.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Command sent from the message thread to the audio thread
 * to start or stop monitoring a session clip's LaunchHandle state.
 *
 * For Monitor commands, launchHandle must point to a valid LaunchHandle
 * whose lifetime is guaranteed by the message thread holding a shared_ptr
 * until the corresponding Unmonitor command is processed.
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
        if (nextWrite == readIdx)
            return false;

        buffer_[writeIdx] = cmd;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
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
    }

  private:
    std::array<SessionClipCommand, kQueueSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
};

}  // namespace magda
