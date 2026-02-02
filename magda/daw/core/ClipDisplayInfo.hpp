#pragma once

#include <algorithm>

#include "ClipInfo.hpp"
#include "ui/utils/TimelineUtils.hpp"

namespace magda {

/**
 * @brief Pre-computed display values derived from ClipInfo + BPM
 *
 * Centralizes all beat-to-seconds, stretch-to-source-file, and loop boundary
 * calculations so that every UI paint/layout path uses consistent values
 * instead of doing inline math.
 */
struct ClipDisplayInfo {
    // Source data (copied for convenience)
    double startTime;      // clip start on timeline (seconds)
    double length;         // clip duration on timeline (seconds)
    double audioOffset;    // file trim offset (source-file seconds)
    double stretchFactor;  // audioStretchFactor

    // Pre-computed display values
    double endTime;  // startTime + length

    // Loop (all in seconds, 0 = no loop)
    bool loopEnabled;
    double loopOffsetSeconds;       // internalLoopOffset converted to seconds
    double loopLengthSeconds;       // internalLoopLength converted to seconds (cycle duration)
    double loopEndPositionSeconds;  // loopOffsetSeconds + loopLengthSeconds (marker position)

    // Source-file ranges for waveform drawing
    double sourceFileStart;  // audioOffset + loopOffsetSeconds/stretchFactor (looped)
                             // or audioOffset (non-looped)
    double sourceFileEnd;    // sourceFileStart + loopLengthSeconds/stretchFactor (looped)
                             // or audioOffset + length/stretchFactor (non-looped)

    // Helpers
    double timelineToSource(double timelineDelta) const {
        return timelineDelta / stretchFactor;
    }

    double sourceToTimeline(double sourceDelta) const {
        return sourceDelta * stretchFactor;
    }

    double maxClipLength(double fileDuration) const {
        return (fileDuration - audioOffset) * stretchFactor;
    }

    bool isLooped() const {
        return loopEnabled && loopLengthSeconds > 0.0 && loopEndPositionSeconds < length;
    }

    // Factory
    static ClipDisplayInfo from(const ClipInfo& clip, double bpm) {
        ClipDisplayInfo d;
        d.startTime = clip.startTime;
        d.length = clip.length;
        d.audioOffset = clip.audioOffset;
        d.stretchFactor = clip.audioStretchFactor;
        d.endTime = clip.startTime + clip.length;

        d.loopEnabled = clip.internalLoopEnabled;
        d.loopOffsetSeconds = TimelineUtils::beatsToSeconds(clip.internalLoopOffset, bpm);
        d.loopLengthSeconds = TimelineUtils::beatsToSeconds(clip.internalLoopLength, bpm);
        d.loopEndPositionSeconds = d.loopOffsetSeconds + d.loopLengthSeconds;

        if (d.loopEnabled && d.loopLengthSeconds > 0.0) {
            d.sourceFileStart = clip.audioOffset + d.loopOffsetSeconds / d.stretchFactor;
            d.sourceFileEnd = d.sourceFileStart + d.loopLengthSeconds / d.stretchFactor;
            // When clip is shorter than one loop cycle, only show what fits
            double maxSourceEnd = d.sourceFileStart + (clip.length / d.stretchFactor);
            d.sourceFileEnd = std::min(d.sourceFileEnd, maxSourceEnd);
        } else {
            d.sourceFileStart = clip.audioOffset;
            d.sourceFileEnd = clip.audioOffset + clip.length / d.stretchFactor;
        }
        return d;
    }
};

}  // namespace magda
