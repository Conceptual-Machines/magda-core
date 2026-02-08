#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace magda {

/**
 * @brief Manages transport state with lock-free thread safety
 *
 * Responsibilities:
 * - Transport playing state (isPlaying, wasPlaying)
 * - Just-started flag (used for one-shot triggers)
 * - Just-looped flag (for loop boundary detection)
 *
 * Thread Safety:
 * - Write: UI thread (transport control callbacks)
 * - Read: Audio thread (every audio callback)
 * - Implementation: std::atomic for all flags, no locks
 */
class TransportStateManager {
  public:
    TransportStateManager() = default;
    ~TransportStateManager() = default;

    /**
     * @brief Update transport state from UI thread
     * @param isPlaying Current transport playing state
     * @param justStarted True if transport just started this frame
     * @param justLooped True if transport just looped this frame
     */
    void updateState(bool isPlaying, bool justStarted, bool justLooped) {
        transportPlaying_.store(isPlaying, std::memory_order_release);
        justStartedFlag_.store(justStarted, std::memory_order_release);
        justLoopedFlag_.store(justLooped, std::memory_order_release);
    }

    /**
     * @brief Get current transport playing state (audio thread safe)
     */
    bool isPlaying() const {
        return transportPlaying_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get just-started flag (audio thread safe)
     */
    bool didJustStart() const {
        return justStartedFlag_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get just-looped flag (audio thread safe)
     */
    bool didJustLoop() const {
        return justLoopedFlag_.load(std::memory_order_acquire);
    }

  private:
    // Transport state (UI thread writes, audio thread reads - lock-free)
    std::atomic<bool> transportPlaying_{false};
    std::atomic<bool> justStartedFlag_{false};
    std::atomic<bool> justLoopedFlag_{false};
};

}  // namespace magda
