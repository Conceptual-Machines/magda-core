#pragma once

#include <array>
#include <atomic>

#include "../core/ClipTypes.hpp"

namespace magda {

/**
 * @brief Event pushed from the audio thread when a session clip's
 * LaunchHandle transitions between playing and stopped.
 */
struct SessionClipStateEvent {
    ClipId clipId = INVALID_CLIP_ID;

    enum class Type : uint8_t { StartedPlaying, StoppedPlaying };
    Type type = Type::StoppedPlaying;

    double transportPositionSeconds = 0.0;  // Exact transport pos at transition
};

/**
 * @brief Lock-free SPSC queue for audio-thread-to-message-thread state events.
 *
 * Audio thread pushes state transition events.
 * Message thread pops and updates UI / ClipManager notifications.
 */
class SessionClipStateQueue {
  public:
    static constexpr int kQueueSize = 64;

    SessionClipStateQueue() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

    bool push(const SessionClipStateEvent& event) {
        int writeIdx = writeIndex_.load(std::memory_order_relaxed);
        int readIdx = readIndex_.load(std::memory_order_acquire);

        int nextWrite = (writeIdx + 1) & (kQueueSize - 1);
        if (nextWrite == readIdx)
            return false;

        buffer_[writeIdx] = event;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }

    bool pop(SessionClipStateEvent& event) {
        int writeIdx = writeIndex_.load(std::memory_order_acquire);
        int readIdx = readIndex_.load(std::memory_order_relaxed);

        if (readIdx == writeIdx)
            return false;

        event = buffer_[readIdx];
        readIndex_.store((readIdx + 1) & (kQueueSize - 1), std::memory_order_release);
        return true;
    }

    void clear() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<SessionClipStateEvent, kQueueSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
};

}  // namespace magda
