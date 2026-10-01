#pragma once

#include <juce_core/juce_core.h>

#include <magda/sdk/lockfree/ParameterQueue.hpp>

#include "../../core/ChainNodePath.hpp"

namespace magda {

/// False when the path has more steps than the queue's fixed payload holds.
inline bool packChainNodePath(const ChainNodePath& path, PackedPath& packed) {
    std::array<PackedPath::Step, PackedPath::kMaxSteps> steps{};
    if (path.steps.size() > steps.size())
        return false;

    for (size_t i = 0; i < path.steps.size(); ++i)
        steps[i] = {static_cast<int>(path.steps[i].type), path.steps[i].id};

    packed.trackId = path.trackId;
    packed.topLevelDeviceId = path.topLevelDeviceId;
    packed.isTrackLevel = path.isTrackLevel;
    return packed.setSteps(steps.data(), path.steps.size());
}

inline ChainNodePath unpackChainNodePath(const PackedPath& packed) {
    ChainNodePath path;
    path.trackId = packed.trackId;
    path.topLevelDeviceId = packed.topLevelDeviceId;
    path.isTrackLevel = packed.isTrackLevel;
    path.steps.reserve(packed.stepCount);
    for (uint8_t i = 0; i < packed.stepCount; ++i)
        path.steps.push_back(
            {static_cast<ChainStepType>(packed.steps[i].type), packed.steps[i].id});
    return path;
}

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
     * @param devicePath MAGDA device path
     * @param paramIndex Parameter index
     * @param value New parameter value
     * @return true if successfully queued, false if queue full
     */
    bool pushChange(const ChainNodePath& devicePath, int paramIndex, float value) {
        ParameterChange change;
        if (!packChainNodePath(devicePath, change.devicePath))
            return false;
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
