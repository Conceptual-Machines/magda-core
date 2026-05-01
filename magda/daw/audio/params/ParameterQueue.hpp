#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

#include "../../core/TypeIds.hpp"

namespace magda {

/**
 * @brief A parameter change request from UI to audio thread
 */
struct ParameterChange {
    DeviceId deviceId = INVALID_DEVICE_ID;
    int paramIndex = -1;
    float value = 0.0f;

    // Optional: for identifying the source of the change
    enum class Source {
        User,        // Direct user interaction
        Macro,       // From macro knob
        Modulation,  // From LFO/modulator
        Automation   // From automation playback
    };
    Source source = Source::User;
};

/**
 * @brief Lock-free SPSC queue for UI-to-audio parameter changes
 *
 * UI thread pushes parameter changes, audio thread pops and applies them.
 * Uses a fixed-size ring buffer for predictable memory behavior.
 */
class ParameterQueue {
  public:
    static constexpr int kQueueSize = 1024;  // Power of 2 for fast modulo

    ParameterQueue() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Push a parameter change (called from UI thread)
     * @param change The parameter change to queue
     * @return true if successfully queued, false if queue full
     */
    bool push(const ParameterChange& change) {
        int writeIdx = writeIndex_.load(std::memory_order_relaxed);
        int readIdx = readIndex_.load(std::memory_order_acquire);

        int nextWrite = (writeIdx + 1) & (kQueueSize - 1);  // Fast modulo for power of 2
        if (nextWrite == readIdx) {
            // Queue full
            return false;
        }

        buffer_[writeIdx] = change;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop a parameter change (called from audio thread)
     * @param change Output parameter for the change
     * @return true if change was available, false if queue empty
     */
    bool pop(ParameterChange& change) {
        int writeIdx = writeIndex_.load(std::memory_order_acquire);
        int readIdx = readIndex_.load(std::memory_order_relaxed);

        if (readIdx == writeIdx) {
            // Queue empty
            return false;
        }

        change = buffer_[readIdx];
        readIndex_.store((readIdx + 1) & (kQueueSize - 1), std::memory_order_release);
        return true;
    }

    /**
     * @brief Check if queue has pending changes
     */
    bool hasPending() const {
        return writeIndex_.load(std::memory_order_acquire) !=
               readIndex_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get approximate number of pending changes
     */
    int pendingCount() const {
        int writeIdx = writeIndex_.load(std::memory_order_acquire);
        int readIdx = readIndex_.load(std::memory_order_relaxed);
        return (writeIdx - readIdx + kQueueSize) & (kQueueSize - 1);
    }

    /**
     * @brief Clear all pending changes (call only when audio is stopped)
     */
    void clear() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<ParameterChange, kQueueSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
};

/**
 * @brief Batched parameter changes for efficiency
 *
 * Groups multiple changes to the same device for processing in one go.
 */
class BatchedParameterQueue {
  public:
    static constexpr int kMaxBatchSize = 64;

    /**
     * @brief Push a batch of changes for a single device
     */
    bool pushBatch(DeviceId deviceId, const std::vector<std::pair<int, float>>& changes) {
        for (const auto& [paramIndex, value] : changes) {
            ParameterChange change;
            change.deviceId = deviceId;
            change.paramIndex = paramIndex;
            change.value = value;
            if (!queue_.push(change)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Pop all pending changes into a vector
     */
    void popAll(std::vector<ParameterChange>& changes) {
        changes.clear();
        ParameterChange change;
        while (queue_.pop(change)) {
            changes.push_back(change);
        }
    }

    ParameterQueue& getQueue() {
        return queue_;
    }
    const ParameterQueue& getQueue() const {
        return queue_;
    }

  private:
    ParameterQueue queue_;
};

}  // namespace magda
