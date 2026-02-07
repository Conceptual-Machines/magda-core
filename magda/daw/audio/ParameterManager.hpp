#pragma once

#include <juce_core/juce_core.h>

#include "../core/TypeIds.hpp"
#include "ParameterQueue.hpp"

namespace magda {

/**
 * @brief Manages parameter changes with lock-free queue
 *
 * Responsibilities:
 * - Parameter change queue (device ID, param index, value)
 * - Lock-free queue management (write from UI, read from audio thread)
 * - Clean interface for parameter dispatch
 *
 * Thread Safety:
 * - Write: UI thread (parameter change notifications)
 * - Read: Audio thread (apply parameter changes)
 * - Implementation: Lock-free FIFO queue (ParameterQueue)
 */
class ParameterManager {
  public:
    ParameterManager() = default;
    ~ParameterManager() = default;

    /**
     * @brief Push a parameter change to the queue (UI thread)
     * @param deviceId MAGDA device ID
     * @param paramIndex Parameter index
     * @param value New parameter value
     * @return true if successfully queued, false if queue full
     */
    bool pushChange(DeviceId deviceId, int paramIndex, float value) {
        ParameterChange change;
        change.deviceId = deviceId;
        change.paramIndex = paramIndex;
        change.value = value;
        return queue_.push(change);
    }

    /**
     * @brief Pop a parameter change from the queue (audio thread)
     * @param change Output parameter for the change
     * @return true if change was available, false if queue empty
     */
    bool popChange(ParameterChange& change) {
        return queue_.pop(change);
    }

    /**
     * @brief Check if queue has pending changes
     */
    bool hasPending() const {
        return queue_.hasPending();
    }

    /**
     * @brief Get approximate number of pending changes
     */
    int pendingCount() const {
        return queue_.pendingCount();
    }

    /**
     * @brief Clear all pending changes (call only when audio is stopped)
     */
    void clear() {
        queue_.clear();
    }

    /**
     * @brief Get direct access to the underlying queue
     * For backward compatibility with existing code
     */
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
