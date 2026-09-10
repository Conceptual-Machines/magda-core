#pragma once

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
 * Three rings because they are three readers of one measurement: the mixer/
 * track UI pops, the recording preview needs its own so the UI doesn't drain
 * data before it reads, and a remote meter subscriber drains to the latest
 * value with the mixer closed.
 */
struct TrackMeters {
    MeteringBuffer mixer, recording, remote;
    std::atomic<float> masterPeakL{0.0f};
    std::atomic<float> masterPeakR{0.0f};
    MidiActivityMonitor midiActivity;

    /// The master strip's level. MASTER_TRACK_ID is negative, so it can't live
    /// in a ring keyed by TrackId.
    void setMasterPeak(float peakL, float peakR) {
        masterPeakL.store(peakL, std::memory_order_relaxed);
        masterPeakR.store(peakR, std::memory_order_relaxed);
    }

    float getMasterPeakL() const {
        return masterPeakL.load(std::memory_order_relaxed);
    }

    float getMasterPeakR() const {
        return masterPeakR.load(std::memory_order_relaxed);
    }
};

}  // namespace magda
