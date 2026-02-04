#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>

#include "ClipInfo.hpp"

namespace magda {

/**
 * @brief Pre-computed display values derived from ClipInfo + BPM
 *
 * Centralizes all stretch-to-source-file and loop boundary calculations
 * so that every UI paint/layout path uses consistent values instead of
 * doing inline math.
 *
 * TE-aligned model fields:
 * - offset: start position in source file (seconds)
 * - loopStart/loopLength: loop region in source file
 * - speedRatio: time stretch ratio
 * - loopEnabled: whether source region loops
 */
struct ClipDisplayInfo {
    // Source data (copied for convenience, using TE terminology)
    double startTime;   // clip start on timeline (seconds)
    double length;      // clip duration on timeline (seconds)
    double offset;      // source file offset (seconds) - TE: Clip::offset
    double speedRatio;  // time stretch ratio - TE: Clip::speedRatio

    // Pre-computed display values
    double endTime;  // startTime + length

    // Source extent (the loop region or derived from clip length)
    double sourceLength;         // loop region length in source-file seconds
    double sourceExtentSeconds;  // sourceLength * speedRatio (visual extent on timeline)

    // Loop (all in seconds) - TE: AudioClipBase loopStart/loopLength
    bool loopEnabled;
    double loopStart;               // where loop starts in source file
    double loopOffset;              // phase within loop region, derived from offset - loopStart
    double loopLengthSeconds;       // loop duration in timeline seconds
    double loopEndPositionSeconds;  // where loop region ends on timeline (for drawing)

    // Source-file ranges for waveform drawing
    double sourceFileStart;  // Where to start reading from source file
    double sourceFileEnd;    // Where to stop reading from source file

    // Helpers
    double timelineToSource(double timelineDelta) const {
        return timelineDelta / speedRatio;
    }

    double sourceToTimeline(double sourceDelta) const {
        return sourceDelta * speedRatio;
    }

    double maxClipLength(double fileDuration) const {
        return (fileDuration - offset) * speedRatio;
    }

    bool isLooped() const {
        return loopEnabled && sourceLength > 0.0;
    }

    // Wrap a value within [0, period)
    static double wrapPhase(double value, double period) {
        if (period <= 0.0)
            return 0.0;
        double result = std::fmod(value, period);
        if (result < 0.0)
            result += period;
        return result;
    }

    // Factory
    // fileDuration is optional - pass 0 if unknown
    static ClipDisplayInfo from(const ClipInfo& clip, double /*bpm*/, double fileDuration = 0.0) {
        ClipDisplayInfo d;
        d.startTime = clip.startTime;
        d.length = clip.length;
        d.offset = clip.offset;
        d.speedRatio = clip.speedRatio;
        d.endTime = clip.startTime + clip.length;

        // Compute source length from loop region or derive from clip
        // Priority: loopLength > fileDuration > clip.length
        if (clip.loopEnabled && clip.loopLength > 0.0) {
            d.sourceLength = clip.loopLength;
            d.loopStart = clip.loopStart;
        } else if (fileDuration > 0.0 && fileDuration > clip.offset) {
            d.sourceLength = fileDuration - clip.offset;
            d.loopStart = clip.offset;
        } else {
            // Fallback: derive from clip length
            d.sourceLength = clip.length / d.speedRatio;
            d.loopStart = clip.offset;
        }
        d.sourceExtentSeconds = d.sourceLength * d.speedRatio;

        d.loopEnabled = clip.loopEnabled;

        // Compute loop offset: phase within the loop region derived from offset - loopStart
        d.loopOffset = wrapPhase(clip.offset - clip.loopStart, d.sourceLength);

        // Loop length in timeline seconds is the source region stretched
        d.loopLengthSeconds = d.sourceLength * d.speedRatio;
        d.loopEndPositionSeconds = d.loopLengthSeconds;  // Loop region starts at 0 in clip-relative

        if (d.loopEnabled && d.sourceLength > 0.0) {
            // In loop mode, offset determines where in the source we start
            double phase = wrapPhase(clip.offset - clip.loopStart, d.sourceLength);
            d.sourceFileStart = d.loopStart + phase;
            d.sourceFileEnd = d.loopStart + d.sourceLength;

            // When clip is shorter than one loop cycle, only show what fits
            double maxSourceEnd = d.sourceFileStart + (clip.length / d.speedRatio);
            if (maxSourceEnd < d.sourceFileEnd) {
                d.sourceFileEnd = maxSourceEnd;
            }
        } else {
            // Non-looped: simple linear mapping from offset
            d.sourceFileStart = clip.offset;
            d.sourceFileEnd = clip.offset + d.sourceLength;
        }

        // DEBUG: Log all values to understand the model
        static int logCount = 0;
        if (++logCount % 100 == 1) {  // Throttle logging
            std::cout << "=== ClipDisplayInfo::from() ===" << std::endl;
            std::cout << "  INPUT (ClipInfo):" << std::endl;
            std::cout << "    offset=" << clip.offset << "s" << std::endl;
            std::cout << "    loopStart=" << clip.loopStart << "s" << std::endl;
            std::cout << "    loopLength=" << clip.loopLength << "s" << std::endl;
            std::cout << "    speedRatio=" << clip.speedRatio << std::endl;
            std::cout << "    startTime=" << clip.startTime << "s, length=" << clip.length << "s"
                      << std::endl;
            std::cout << "    loopEnabled=" << clip.loopEnabled << std::endl;
            std::cout << "  OUTPUT (ClipDisplayInfo):" << std::endl;
            std::cout << "    sourceLength=" << d.sourceLength << "s" << std::endl;
            std::cout << "    sourceExtentSeconds=" << d.sourceExtentSeconds << "s" << std::endl;
            std::cout << "    loopLengthSeconds=" << d.loopLengthSeconds << "s" << std::endl;
            std::cout << "    sourceFileStart=" << d.sourceFileStart << "s" << std::endl;
            std::cout << "    sourceFileEnd=" << d.sourceFileEnd << "s" << std::endl;
            std::cout << "===============================" << std::endl;
        }

        return d;
    }
};

}  // namespace magda
