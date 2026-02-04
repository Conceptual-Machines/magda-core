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
    double loopStartPositionSeconds;  // loop start position in editor (0 when looped, relative to
                                      // offset otherwise)
    double loopEndPositionSeconds;    // loopStartPositionSeconds + loopLengthSeconds
    double offsetPositionSeconds;  // playback start position relative to display anchor (loopStart
                                   // when looped)

    // Full source extent (from display anchor to file end, for waveform editor)
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
        return sourceDelta / speedRatio;
    }

    double maxClipLength(double fileDuration) const {
        if (autoTempo && sourceLength > 0.0 && sourceExtentSeconds > 0.0) {
            return (fileDuration - offset) * sourceExtentSeconds / sourceLength;
        }
        return (fileDuration - offset) / speedRatio;
    }

    bool isLooped() const {
        return loopEnabled && sourceLength > 0.0;
    }

    // Convert a timeline position (relative to display anchor) to absolute source file time
    double displayPositionToSourceTime(double timelinePos) const {
        double anchor = isLooped() ? loopStart : offset;
        return anchor + timelineToSource(timelinePos);
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
    static ClipDisplayInfo from(const ClipInfo& clip, double bpm, double fileDuration = 0.0) {
        ClipDisplayInfo d;
        d.startTime = clip.startTime;
        d.length = clip.length;
        d.offset = clip.offset;
        d.speedRatio = clip.speedRatio;
        d.endTime = clip.startTime + clip.length;

        // Auto-tempo display info (using centralized ClipInfo methods)
        d.autoTempo = clip.autoTempo;
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

        if (clip.loopEnabled && clip.loopLength > 0.0) {
            // In loop mode, anchor display at loopStart.
            // Loop starts at position 0, offset is shown as a phase marker.
            d.loopStartPositionSeconds = 0.0;
            d.loopEndPositionSeconds = d.loopLengthSeconds;
            d.offsetPositionSeconds = srcToTimeline(clip.offset - clip.loopStart);

            // Full source extent from loopStart to file end
            if (fileDuration > 0.0 && fileDuration > clip.loopStart) {
                d.fullSourceExtentSeconds = srcToTimeline(fileDuration - clip.loopStart);
            } else {
                d.fullSourceExtentSeconds = d.sourceExtentSeconds;
            }
        } else {
            // Non-loop: anchor at offset
            d.loopStartPositionSeconds = std::max(0.0, srcToTimeline(clip.loopStart - clip.offset));
            d.loopEndPositionSeconds = d.loopStartPositionSeconds + d.loopLengthSeconds;
            d.offsetPositionSeconds = 0.0;  // offset IS position 0

            // Full source extent from offset to file end
            if (fileDuration > 0.0 && fileDuration > clip.offset) {
                d.fullSourceExtentSeconds = srcToTimeline(fileDuration - clip.offset);
            } else {
                d.fullSourceExtentSeconds = d.sourceExtentSeconds;
            }
        }

        if (d.loopEnabled && d.sourceLength > 0.0) {
            // In loop mode, show the full loop region from loopStart.
            // Phase (offset - loopStart) only affects playback position,
            // not the displayed source range.
            d.sourceFileStart = d.loopStart;
            d.sourceFileEnd = d.loopStart + d.sourceLength;

            // Clamp to file bounds
            if (fileDuration > 0.0 && d.sourceFileEnd > fileDuration) {
                d.sourceFileEnd = fileDuration;
            }
        } else {
            // Non-looped: simple linear mapping from offset
            d.sourceFileStart = clip.offset;
            d.sourceFileEnd = clip.offset + d.sourceLength;
        }

        // Effective source extent: visual boundary with fallback chain
        d.effectiveSourceExtentSeconds = d.fullSourceExtentSeconds;
        if (d.effectiveSourceExtentSeconds <= 0.0)
            d.effectiveSourceExtentSeconds = d.sourceExtentSeconds;
        if (d.effectiveSourceExtentSeconds <= 0.0)
            d.effectiveSourceExtentSeconds = clip.length;

        // Full drawable source-file range (extends to file end in loop mode)
        d.fullDrawStartSeconds = d.sourceFileStart;
        if (d.isLooped() && fileDuration > 0.0) {
            d.fullDrawEndSeconds = fileDuration;
        } else {
            d.fullDrawEndSeconds = d.sourceFileEnd;
        }

        return d;
    }
};

}  // namespace magda
