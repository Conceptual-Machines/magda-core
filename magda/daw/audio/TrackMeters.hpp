#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

#include "MeteringBuffer.hpp"
#include "midi/MidiActivityMonitor.hpp"

/**
 * @file TrackMeters.hpp
 * @brief What the UI reads about each track's level and MIDI activity (#2579).
 */

namespace magda {

/**
 * @brief Per-track level and MIDI-activity data, fed at frame rate by whichever
 * engine renders.
 *
 * The mixer and recording preview have independent rings. Remote reads need
 * the latest value even after a long idle period, so they use atomics instead
 * of a ring that stops accepting writes when it fills.
 */
struct TrackMeters {
    MeteringBuffer mixer, recording;
    struct LatestLevel {
        std::atomic<float> peakL{0.0f}, peakR{0.0f};
        std::atomic<bool> clipped{false}, available{false};
        std::atomic<juce::uint32> publishedAtMs{0};
    };
    std::array<LatestLevel, MeteringBuffer::kMaxTracks> remoteLatest;
    std::atomic<float> masterPeakL{0.0f};
    std::atomic<float> masterPeakR{0.0f};
    std::atomic<bool> masterPublished{false};
    std::atomic<juce::uint32> masterPublishedAtMs{0};
    MidiActivityMonitor midiActivity;

    void setRemotePeak(TrackId id, const MeterData& data) {
        if (id < 0 || id >= MeteringBuffer::kMaxTracks)
            return;
        auto& level = remoteLatest[static_cast<size_t>(id)];
        level.peakL.store(data.peakL, std::memory_order_relaxed);
        level.peakR.store(data.peakR, std::memory_order_relaxed);
        level.clipped.store(data.clipped, std::memory_order_relaxed);
        level.publishedAtMs.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        level.available.store(true, std::memory_order_release);
    }

    void clearRemotePeaks() {
        for (auto& level : remoteLatest)
            level.available.store(false, std::memory_order_release);
        clearMasterPeak();
    }

    /// The master strip's level. MASTER_TRACK_ID is negative, so it can't live
    /// in a ring keyed by TrackId.
    void setMasterPeak(float peakL, float peakR) {
        masterPeakL.store(peakL, std::memory_order_relaxed);
        masterPeakR.store(peakR, std::memory_order_relaxed);
        masterPublishedAtMs.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        masterPublished.store(true, std::memory_order_release);
    }

    float getMasterPeakL() const {
        return masterPeakL.load(std::memory_order_relaxed);
    }

    float getMasterPeakR() const {
        return masterPeakR.load(std::memory_order_relaxed);
    }

    bool hasMasterPeak() const {
        return masterPublished.load(std::memory_order_acquire);
    }
    void clearMasterPeak() {
        masterPublished.store(false, std::memory_order_release);
    }
};

}  // namespace magda
