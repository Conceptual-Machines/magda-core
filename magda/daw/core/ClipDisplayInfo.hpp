#pragma once

#include <algorithm>
#include <cmath>

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
                        // NOTE: In autoTempo mode, speedRatio is always 1.0 (TE handles stretching)

    // Pre-computed display values
    double endTime;  // startTime + length

    // Source extent (the loop region or derived from clip length)
    double sourceLength;         // loop region length in source-file seconds
    double sourceExtentSeconds;  // sourceLength * speedRatio (visual extent on timeline)

    // Loop (all in seconds) - TE: AudioClipBase loopStart/loopLength
    bool loopEnabled;
    double loopStart;          // where loop starts in source file
    double loopOffset;         // phase within loop region, derived from offset - loopStart
    double loopLengthSeconds;  // loop duration in timeline seconds (from clip's actual loopLength)
    double loopStartPositionSeconds;  // loop start position (absolute source position in timeline
                                      // seconds)
    double loopEndPositionSeconds;    // loopStartPositionSeconds + loopLengthSeconds
    double offsetPositionSeconds;     // offset position in timeline seconds (from file start)
    double loopPhasePositionSeconds;  // phase position in timeline seconds (loopStart + phase)

    // Full source extent (from file start to file end, for waveform editor)
    double fullSourceExtentSeconds;

    // Source-file ranges for waveform drawing
    double sourceFileStart;  // Where to start reading from source file
    double sourceFileEnd;    // Where to stop reading from source file

    // Pre-computed display helpers
    double effectiveSourceExtentSeconds;  // Visual boundary extent with fallback chain baked in
    double fullDrawStartSeconds;          // Full drawable source-file range start
    double fullDrawEndSeconds;  // Full drawable source-file range end (extends to file end in loop
                                // mode)

    // Auto-tempo (musical mode) display
    bool autoTempo = false;        // Whether clip uses beat-based length
    double lengthBeats = 0.0;      // Clip timeline length in project beats
    double loopLengthBeats = 0.0;  // Loop length in beats (when autoTempo=true)
    double startBeats = 0.0;       // Start position in beats
    double endBeats = 0.0;         // End position in beats

    // Helpers
    // For manual stretch: speedRatio is a SPEED FACTOR (timeline = source / speedRatio)
    // For autoTempo: uses sourceLength/sourceExtentSeconds ratio instead of speedRatio
    double timelineToSource(double timelineDelta) const {
        if (autoTempo && sourceExtentSeconds > 0.0) {
            return timelineDelta * sourceLength / sourceExtentSeconds;
        }
        return timelineDelta * speedRatio;
    }

    double sourceToTimeline(double sourceDelta) const {
        if (autoTempo && sourceLength > 0.0) {
            return sourceDelta * sourceExtentSeconds / sourceLength;
        }
        if (speedRatio <= 0.0)
            return 0.0;
        return sourceDelta / speedRatio;
    }

    double maxClipLength(double fileDuration) const {
        if (autoTempo && sourceLength > 0.0 && sourceExtentSeconds > 0.0) {
            return (fileDuration - offset) * sourceExtentSeconds / sourceLength;
        }
        if (speedRatio <= 0.0)
            return 0.0;
        return (fileDuration - offset) / speedRatio;
    }

    bool isLooped() const {
        return loopEnabled && sourceLength > 0.0;
    }

    // Convert a timeline position (relative to display anchor = file start) to absolute source file
    // time
    double displayPositionToSourceTime(double timelinePos) const {
        return timelineToSource(timelinePos);
    }

    // Factory
    // fileDuration is optional - pass 0 if unknown
    static ClipDisplayInfo from(const ClipInfo& clip, double bpm, double fileDuration = 0.0) {
        ClipDisplayInfo d;
        d.startTime = clip.startTime;
        d.length = clip.length;
        d.offset = clip.offset;
        d.speedRatio = clip.speedRatio;
        d.endTime = clip.startTime + clip.length;

        // Auto-tempo display info (using centralized ClipInfo methods)
        d.autoTempo = clip.autoTempo;
        d.lengthBeats = clip.lengthBeats;
        d.loopLengthBeats = clip.loopLengthBeats;
        d.startBeats = clip.getStartBeats(bpm);
        d.endBeats = clip.getEndBeats(bpm);

        // Compute source length from loop region or derive from clip
        // Priority: loopLength > fileDuration > clip.length
        // SPECIAL CASE: In autoTempo mode, clip.length is timeline duration (changes with BPM)
        // but we need the actual SOURCE audio length (which stays constant)
        if (clip.autoTempo && clip.loopLength > 0.0) {
            // Musical mode: loopLength IS the source audio length
            d.sourceLength = clip.loopLength;
            d.loopStart = clip.loopStart;
        } else if (clip.loopEnabled && clip.loopLength > 0.0) {
            d.sourceLength = clip.loopLength;
            d.loopStart = clip.loopStart;
            // Clamp loop region to available audio
            if (fileDuration > 0.0 && d.loopStart + d.sourceLength > fileDuration) {
                d.sourceLength = std::max(0.001, fileDuration - d.loopStart);
            }
        } else if (fileDuration > 0.0 && fileDuration > clip.offset) {
            d.sourceLength = fileDuration - clip.offset;
            d.loopStart = clip.offset;
        } else {
            // Fallback: derive from clip length
            d.sourceLength = clip.timelineToSource(clip.length);
            d.loopStart = clip.offset;
        }
        // Convert source-file duration to timeline duration.
        // For autoTempo clips: derived from beat values and BPM (speedRatio is irrelevant,
        // TE handles stretching). For manual stretch: uses speedRatio as usual.
        auto srcToTimeline = [&](double sourceDelta) -> double {
            if (clip.autoTempo && clip.loopLength > 0.0 && clip.loopLengthBeats > 0.0 &&
                bpm > 0.0) {
                return sourceDelta * (clip.loopLengthBeats * 60.0 / bpm) / clip.loopLength;
            }
            return (clip.speedRatio > 0.0) ? sourceDelta / clip.speedRatio : 0.0;
        };

        d.sourceExtentSeconds = srcToTimeline(d.sourceLength);

        d.loopEnabled = clip.loopEnabled;

        // Compute loop offset: phase within the loop region derived from offset - loopStart
        d.loopOffset = wrapPhase(clip.offset - clip.loopStart, d.sourceLength);

        d.loopLengthSeconds = (clip.loopLength > 0.0) ? srcToTimeline(clip.loopLength) : 0.0;

        // Anchor display at source file start (position 0 = file start).
        // All positions are absolute source positions converted to timeline seconds.
        d.loopStartPositionSeconds = srcToTimeline(clip.loopStart);
        d.loopEndPositionSeconds = d.loopStartPositionSeconds + d.loopLengthSeconds;
        d.offsetPositionSeconds = srcToTimeline(clip.offset);
        d.loopPhasePositionSeconds = d.loopStartPositionSeconds + srcToTimeline(d.loopOffset);

        // Full source extent from file start to file end
        if (fileDuration > 0.0) {
            d.fullSourceExtentSeconds = srcToTimeline(fileDuration);
        } else {
            d.fullSourceExtentSeconds = d.sourceExtentSeconds;
        }

        // Source file range: the source region relevant for waveform drawing
        // Looped: the loop region (loopStart to loopStart + loopLength)
        // Non-looped: from offset to offset + sourceLength
        if (clip.loopEnabled && clip.loopLength > 0.0) {
            d.sourceFileStart = clip.loopStart;
            d.sourceFileEnd = clip.loopStart + d.sourceLength;
        } else {
            d.sourceFileStart = clip.offset;
            d.sourceFileEnd = clip.offset + d.sourceLength;
        }

        // Clamp to file bounds
        if (fileDuration > 0.0 && d.sourceFileEnd > fileDuration) {
            d.sourceFileEnd = fileDuration;
        }

        // Effective source extent: visual boundary with fallback chain
        d.effectiveSourceExtentSeconds = d.fullSourceExtentSeconds;
        if (d.effectiveSourceExtentSeconds <= 0.0)
            d.effectiveSourceExtentSeconds = d.sourceExtentSeconds;
        if (d.effectiveSourceExtentSeconds <= 0.0)
            d.effectiveSourceExtentSeconds = clip.length;

        // Full drawable source-file range: always from file start
        d.fullDrawStartSeconds = 0.0;
        if (fileDuration > 0.0) {
            d.fullDrawEndSeconds = fileDuration;
        } else {
            d.fullDrawEndSeconds = d.sourceFileEnd;
        }

        return d;
    }
};

}  // namespace magda
