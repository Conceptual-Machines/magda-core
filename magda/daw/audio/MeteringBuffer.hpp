#pragma once

#include <array>
#include <atomic>
#include <cmath>

#include "../core/TypeIds.hpp"

namespace magda {

/**
 * @brief Per-track metering data
 */
struct MeterData {
    float peakL = 0.0f;    // Left channel peak level (0.0 - 1.0+)
    float peakR = 0.0f;    // Right channel peak level (0.0 - 1.0+)
    float rmsL = 0.0f;     // Left channel RMS level (0.0 - 1.0+)
    float rmsR = 0.0f;     // Right channel RMS level (0.0 - 1.0+)
    bool clipped = false;  // True if either channel exceeded 1.0

    void reset() {
        peakL = peakR = rmsL = rmsR = 0.0f;
        clipped = false;
    }
};

/**
 * @brief Lock-free SPSC ring buffer for track metering data
 *
 * Audio thread pushes meter readings, UI thread pops them.
 * Uses double-buffering per track for simplicity and low latency.
 */
class MeteringBuffer {
  public:
    static constexpr int kMaxTracks = 128;
    static constexpr int kBufferSize = 8;  // Ring buffer size per track

    MeteringBuffer() {
        // Initialize all track buffers
        for (int i = 0; i < kMaxTracks; ++i) {
            trackBuffers_[i].writeIndex.store(0);
            trackBuffers_[i].readIndex.store(0);
        }
    }

    /**
     * @brief Push meter data for a track (called from audio thread)
     * @param trackId The track to push data for
     * @param data The meter data
     * @return true if successfully pushed, false if buffer full
     */
    bool pushLevels(TrackId trackId, const MeterData& data) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return false;

        auto& buffer = trackBuffers_[trackId];
        int writeIdx = buffer.writeIndex.load(std::memory_order_relaxed);
        int readIdx = buffer.readIndex.load(std::memory_order_acquire);

        int nextWrite = (writeIdx + 1) % kBufferSize;
        if (nextWrite == readIdx) {
            // Buffer full, drop oldest
            return false;
        }

        buffer.data[writeIdx] = data;
        buffer.writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop meter data for a track (called from UI thread)
     * @param trackId The track to pop data for
     * @param data Output parameter for the meter data
     * @return true if data was available, false if buffer empty
     */
    bool popLevels(TrackId trackId, MeterData& data) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return false;

        auto& buffer = trackBuffers_[trackId];
        int writeIdx = buffer.writeIndex.load(std::memory_order_acquire);
        int readIdx = buffer.readIndex.load(std::memory_order_relaxed);

        if (readIdx == writeIdx) {
            // Buffer empty
            return false;
        }

        data = buffer.data[readIdx];
        buffer.readIndex.store((readIdx + 1) % kBufferSize, std::memory_order_release);
        return true;
    }

    /**
     * @brief Get latest meter data for a track without removing it
     * @param trackId The track to peek data for
     * @param data Output parameter for the meter data
     * @return true if data was available, false if buffer empty
     */
    bool peekLatest(TrackId trackId, MeterData& data) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return false;

        auto& buffer = trackBuffers_[trackId];
        int writeIdx = buffer.writeIndex.load(std::memory_order_acquire);
        int readIdx = buffer.readIndex.load(std::memory_order_relaxed);

        if (readIdx == writeIdx) {
            return false;
        }

        // Get the most recent data (one before write index)
        int latestIdx = (writeIdx - 1 + kBufferSize) % kBufferSize;
        data = buffer.data[latestIdx];
        return true;
    }

    /**
     * @brief Drain all pending data for a track, keeping only the latest
     * @param trackId The track to drain
     * @param data Output parameter for the latest meter data
     * @return true if any data was available
     */
    bool drainToLatest(TrackId trackId, MeterData& data) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return false;

        bool hasData = false;
        while (popLevels(trackId, data)) {
            hasData = true;
        }
        return hasData;
    }

    /**
     * @brief Clear all data for a track
     */
    void clearTrack(TrackId trackId) {
        if (trackId < 0 || trackId >= kMaxTracks)
            return;

        auto& buffer = trackBuffers_[trackId];
        buffer.writeIndex.store(0, std::memory_order_relaxed);
        buffer.readIndex.store(0, std::memory_order_relaxed);
    }

  private:
    struct TrackBuffer {
        std::array<MeterData, kBufferSize> data{};
        std::atomic<int> writeIndex{0};
        std::atomic<int> readIndex{0};
    };

    std::array<TrackBuffer, kMaxTracks> trackBuffers_;
};

/**
 * @brief Helper class to accumulate RMS values over time
 */
class RMSAccumulator {
  public:
    RMSAccumulator(int windowSizeInSamples = 1024) : windowSize_(windowSizeInSamples) {
        reset();
    }

    void reset() {
        sumSquaresL_ = 0.0;
        sumSquaresR_ = 0.0;
        sampleCount_ = 0;
    }

    void addSample(float left, float right) {
        sumSquaresL_ += left * left;
        sumSquaresR_ += right * right;
        sampleCount_++;
    }

    void addBlock(const float* leftChannel, const float* rightChannel, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            float l = leftChannel ? leftChannel[i] : 0.0f;
            float r = rightChannel ? rightChannel[i] : 0.0f;
            sumSquaresL_ += l * l;
            sumSquaresR_ += r * r;
        }
        sampleCount_ += numSamples;
    }

    bool isWindowComplete() const {
        return sampleCount_ >= windowSize_;
    }

    float getRMSL() const {
        return sampleCount_ > 0 ? std::sqrt(static_cast<float>(sumSquaresL_ / sampleCount_)) : 0.0f;
    }

    float getRMSR() const {
        return sampleCount_ > 0 ? std::sqrt(static_cast<float>(sumSquaresR_ / sampleCount_)) : 0.0f;
    }

    int getSampleCount() const {
        return sampleCount_;
    }

  private:
    int windowSize_;
    double sumSquaresL_;
    double sumSquaresR_;
    int sampleCount_;
};

}  // namespace magda
