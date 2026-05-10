#pragma once

#include <algorithm>
#include <cmath>

#include "ClipInfo.hpp"

namespace magda {

/**
 * @brief Pre-computed display values derived from ClipInfo + BPM.
 *
 * Single source of truth for the waveform editor / clip-rendering paths.
 *
 * Field meanings are deliberately one-purpose. The previous version
 * overloaded "sourceLength" and "sourceFileStart/End" to mean both the
 * displayable source range AND the active loop region — which is what
 * caused the editor to truncate the waveform whenever the loop region
 * was smaller than the source file. The clean contract:
 *
 *   - File extent fields  → always describe the source file on disk.
 *                           Renderers should draw THIS range, full stop.
 *   - Loop region fields  → describe the looping subset within the file.
 *                           Drawn as overlays, never gate the file extent.
 *
 * If you find yourself computing "drawable" or "visible" range from a
 * loop field, that's the bug returning. Reach for the file-extent fields.
 */
struct ClipDisplayInfo {
    // ------- Clip placement on the timeline -------
    double startTime;   // clip start on timeline (seconds)
    double length;      // clip duration on timeline (seconds)
    double endTime;     // startTime + length
    double offset;      // source file offset (seconds) — TE: Clip::offset
    double speedRatio;  // time stretch ratio — TE: Clip::speedRatio
                        // In autoTempo mode, speedRatio is always 1.0 (TE handles stretching).

    // ------- Source file extent (the only "what to draw" answer) -------
    // Always describes the source audio on disk; never a loop subset.
    // When fileDuration is unknown the factory falls back to the clip's
    // own length so callers always get a sensible draw range.
    double sourceFileStart;  // start of the drawable file range (source-time, seconds)
    double sourceFileEnd;    // end of the drawable file range (source-time, seconds)

    // ------- Loop region (only valid when loopEnabled) -------
    // The active loop subset within the file. Renderers should draw loop
    // brackets / dim out-of-loop content using these — never use them to
    // gate the file extent above.
    bool loopEnabled;
    double loopRegionStartSource;   // loop start in source-time
    double loopRegionLengthSource;  // loop length in source-time

    // Loop region in timeline coordinates, pre-converted for overlay
    // drawing (kept as separate fields because the renderer reads them
    // every paint and the conversion ratio is constant per-clip).
    double loopStartPositionSeconds;  // loop start, anchored at file start = 0
    double loopEndPositionSeconds;    // loopStartPositionSeconds + loopLengthSeconds
    double loopLengthSeconds;         // loop length in timeline-time

    // ------- Offset / phase (for playhead and orange-dot overlays) -------
    double offsetPositionSeconds;     // offset in timeline coords (from file start)
    double loopOffset;                // phase within the loop region (source-time)
    double loopPhasePositionSeconds;  // phase position in timeline coords

    // ------- Source-time ↔ timeline-time conversion -------
    // One ratio drives every conversion. Computed once in the factory so
    // sourceToTimeline / timelineToSource don't have to re-resolve the
    // autoTempo / speedRatio branch on every call.
    //   timeline = source * srcToTimelineRatio
    //   source   = timeline / srcToTimelineRatio
    double srcToTimelineRatio;

    // ------- Auto-tempo / musical-mode display -------
    bool autoTempo = false;
    double lengthBeats = 0.0;      // clip timeline length in project beats
    double loopLengthBeats = 0.0;  // loop length in beats (when autoTempo=true)
    double startBeats = 0.0;       // start position in beats
    double endBeats = 0.0;         // end position in beats

    // ============================================================
    // Helpers
    // ============================================================

    double sourceToTimeline(double sourceDelta) const {
        return sourceDelta * srcToTimelineRatio;
    }

    double timelineToSource(double timelineDelta) const {
        return (srcToTimelineRatio > 0.0) ? timelineDelta / srcToTimelineRatio : 0.0;
    }

    /// Total source file extent in source-time.
    double fileExtentSource() const {
        return sourceFileEnd - sourceFileStart;
    }

    /// Total source file extent in timeline-time.
    double fileExtentTimeline() const {
        return sourceToTimeline(fileExtentSource());
    }

    /// Maximum clip length given the file's duration and the current offset.
    double maxClipLength(double fileDuration) const {
        if (srcToTimelineRatio <= 0.0)
            return 0.0;
        return (fileDuration - offset) * srcToTimelineRatio;
    }

    bool isLooped() const {
        return loopEnabled && loopRegionLengthSource > 0.0;
    }

    /// Where on the editor's timeline ruler the session playhead is for a
    /// given session-time. Looped clips wrap around the loop region.
    double sessionPlayheadToDisplayPosition(double sessionPlayheadSeconds) const {
        if (sessionPlayheadSeconds < 0.0)
            return -1.0;

        if (isLooped() && loopLengthSeconds > 0.0) {
            const double phaseDisplay = loopPhasePositionSeconds - loopStartPositionSeconds;
            double wrappedDisplay =
                std::fmod(phaseDisplay + sessionPlayheadSeconds, loopLengthSeconds);
            if (wrappedDisplay < 0.0)
                wrappedDisplay += loopLengthSeconds;
            return loopStartPositionSeconds + wrappedDisplay;
        }

        return offsetPositionSeconds + sessionPlayheadSeconds;
    }

    /// Convert a timeline position (timeline-seconds, anchored at file start = 0)
    /// to absolute source-file time.
    double displayPositionToSourceTime(double timelinePos) const {
        return timelineToSource(timelinePos);
    }

    // ============================================================
    // Factory
    // ============================================================
    //
    // fileDuration is optional; pass 0 if unknown.
    //
    // Issue #1157: every seconds-domain value read from `clip` is routed
    // through ClipInfo accessors (getTimelineLength / getSourceLoopStart /
    // getSourceLoopLength / getSourceOffset) so autoTempo clips compute
    // live from beats × BPM and stay correct after BPM changes.
    static ClipDisplayInfo from(const ClipInfo& clip, double bpm, double fileDuration = 0.0) {
        ClipDisplayInfo d{};

        const double clipLength = clip.getTimelineLength(bpm);
        const double clipStart = clip.getTimelineStart(bpm);
        const double clipOffset = clip.getSourceOffset();
        const double clipLoopStart = clip.getSourceLoopStart();
        const double clipLoopLength = clip.getSourceLoopLength();

        d.startTime = clipStart;
        d.length = clipLength;
        d.endTime = clipStart + clipLength;
        d.offset = clipOffset;
        d.speedRatio = clip.speedRatio;

        d.autoTempo = clip.autoTempo;
        d.lengthBeats = clip.placement.lengthBeats;
        d.loopLengthBeats = clip.loopLengthBeats;
        d.startBeats = clip.getStartBeats(bpm);
        d.endBeats = clip.getEndBeats(bpm);

        // ---- Source-time ↔ timeline-time conversion ratio ----
        //
        // AutoTempo: TE stretches the source so 1 source beat == 1 timeline
        // beat, so timelineSeconds = sourceSeconds × (sourceBPM / projectBPM).
        // We try the source-interpretation BPM first; if absent, fall back to
        // the loop's beat-count for the same calibration. Issue #1157.
        //
        // Manual stretch: timelineSeconds = sourceSeconds / speedRatio.
        if (clip.autoTempo && clip.audio().interpretation.bpm > 0.0 && bpm > 0.0) {
            d.srcToTimelineRatio = clip.audio().interpretation.bpm / bpm;
        } else if (clip.autoTempo && clipLoopLength > 0.0 && clip.loopLengthBeats > 0.0 &&
                   bpm > 0.0) {
            d.srcToTimelineRatio = (clip.loopLengthBeats * 60.0 / bpm) / clipLoopLength;
        } else {
            d.srcToTimelineRatio = (clip.speedRatio > 0.0) ? 1.0 / clip.speedRatio : 1.0;
        }

        // ---- Source file extent (always [0, fileDuration]) ----
        d.sourceFileStart = 0.0;
        if (fileDuration > 0.0) {
            d.sourceFileEnd = fileDuration;
        } else {
            // No thumbnail / file metadata available — fall back to a
            // conservative range derived from the clip's own length so
            // the editor still gets something sane to draw.
            const double sourceLenFromLength =
                (clip.speedRatio > 0.0) ? clipLength * clip.speedRatio : clipLength;
            d.sourceFileEnd = std::max(clipOffset + sourceLenFromLength, clipOffset + 0.001);
        }

        // ---- Loop region ----
        d.loopEnabled = clip.loopEnabled;
        d.loopRegionStartSource = clipLoopStart;
        d.loopRegionLengthSource = clipLoopLength;

        // Sentinel: in the rest of the codebase, `loopLength == 0` while
        // `loopEnabled` is true means "loop the whole remaining source"
        // — the playback path (SessionClipScheduler etc.) treats it that
        // way and it shows up on older / imported / freshly-toggled
        // clips. We mirror that here so the editor / playhead / overlays
        // don't read the same clip as non-looped while it's actively
        // looping in audio. Without this fallback, isLooped() returns
        // false for any zero-length sentinel.
        if (clip.loopEnabled && d.loopRegionLengthSource <= 0.0) {
            const double anchor = std::max(d.loopRegionStartSource, d.sourceFileStart);
            d.loopRegionStartSource = anchor;
            d.loopRegionLengthSource = std::max(0.0, d.sourceFileEnd - anchor);
        }

        // Clamp loop region to file bounds when known. Don't shrink the
        // file-extent fields — only the loop region itself.
        if (fileDuration > 0.0) {
            d.loopRegionStartSource = std::min(d.loopRegionStartSource, fileDuration);
            if (d.loopRegionStartSource + d.loopRegionLengthSource > fileDuration) {
                d.loopRegionLengthSource = std::max(0.0, fileDuration - d.loopRegionStartSource);
            }
        }

        // ---- Phase / offset ----
        // loopOffset = where in the loop region playback starts, relative
        // to loopRegionStartSource; wrapped into [0, loopLength).
        d.loopOffset =
            (d.loopRegionLengthSource > 0.0)
                ? wrapPhase(clipOffset - d.loopRegionStartSource, d.loopRegionLengthSource)
                : 0.0;

        // ---- Loop region in timeline-time ----
        d.loopStartPositionSeconds = d.sourceToTimeline(d.loopRegionStartSource);
        d.loopLengthSeconds = d.sourceToTimeline(d.loopRegionLengthSource);
        d.loopEndPositionSeconds = d.loopStartPositionSeconds + d.loopLengthSeconds;
        d.offsetPositionSeconds = d.sourceToTimeline(clipOffset);
        d.loopPhasePositionSeconds = d.loopStartPositionSeconds + d.sourceToTimeline(d.loopOffset);

        return d;
    }
};

}  // namespace magda
