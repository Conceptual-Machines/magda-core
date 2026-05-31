#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../../core/ChainNodePath.hpp"

namespace magda {

/**
 * Fixed-capacity path payload for the realtime parameter queue.
 *
 * ChainNodePath owns a std::vector, so copying it through the ring can allocate
 * and mutate per-slot heap state. Keep the queued representation inline and
 * reconstruct a ChainNodePath only after popping, outside the queue storage.
 */
struct PackedChainNodePath {
    static constexpr size_t kMaxSteps = 8;

    TrackId trackId = INVALID_TRACK_ID;
    DeviceId topLevelDeviceId = INVALID_DEVICE_ID;
    bool isTrackLevel = false;
    uint8_t stepCount = 0;
    std::array<ChainPathStep, kMaxSteps> steps{};

    bool assign(const ChainNodePath& path) {
        if (path.steps.size() > kMaxSteps)
            return false;

        trackId = path.trackId;
        topLevelDeviceId = path.topLevelDeviceId;
        isTrackLevel = path.isTrackLevel;
        stepCount = static_cast<uint8_t>(path.steps.size());
        for (size_t i = 0; i < path.steps.size(); ++i)
            steps[i] = path.steps[i];
        return true;
    }

    PackedChainNodePath& operator=(const ChainNodePath& path) {
        const bool assigned = assign(path);
        jassert(assigned);
        juce::ignoreUnused(assigned);
        return *this;
    }

    ChainNodePath toPath() const {
        ChainNodePath path;
        path.trackId = trackId;
        path.topLevelDeviceId = topLevelDeviceId;
        path.isTrackLevel = isTrackLevel;
        path.steps.reserve(stepCount);
        for (uint8_t i = 0; i < stepCount; ++i)
            path.steps.push_back(steps[i]);
        return path;
    }

    operator ChainNodePath() const {
        return toPath();
    }

    DeviceId getDeviceId() const {
        if (topLevelDeviceId != INVALID_DEVICE_ID)
            return topLevelDeviceId;
        if (stepCount > 0 && steps[stepCount - 1].type == ChainStepType::Device)
            return steps[stepCount - 1].id;
        return INVALID_DEVICE_ID;
    }
};

/**
 * @brief A parameter change request from UI to audio thread
 */
struct ParameterChange {
    PackedChainNodePath devicePath;
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

    bool setDevicePath(const ChainNodePath& path) {
        return devicePath.assign(path);
    }

    ChainNodePath getDevicePath() const {
        return devicePath.toPath();
    }
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
    bool pushBatch(const ChainNodePath& devicePath,
                   const std::vector<std::pair<int, float>>& changes) {
        for (const auto& [paramIndex, value] : changes) {
            ParameterChange change;
            if (!change.setDevicePath(devicePath))
                return false;
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
