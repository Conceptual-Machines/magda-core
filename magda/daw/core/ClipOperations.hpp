#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <utility>

#include "ClipInfo.hpp"

namespace magda {

/**
 * @brief Centralized utility class for all clip operations
 *
 * Provides static methods for:
 * - Container operations (clip boundaries only)
 * - Audio trim/stretch operations (clip-level fields)
 * - Compound operations (both container and content)
 * - Coordinate transformations and boundary constraints
 *
 * TE-aligned model behavior:
 * - Non-looped resize left: adjusts offset to keep content at timeline position
 * - Looped resize left: adjusts offset (wrapped within loop region) to keep content at timeline
 * position
 * - Resize right: only changes length (more/fewer loop cycles for looped)
 *
 * All methods are stateless and modify data structures in place.
 */
class ClipOperations {
  public:
    // ========================================================================
    // Constraint Constants
    // ========================================================================

    static constexpr double MIN_CLIP_LENGTH = ClipInfo::MIN_CLIP_LENGTH;
    static constexpr double MIN_SOURCE_LENGTH = 0.01;
    static constexpr double MIN_SPEED_RATIO = 0.25;
    static constexpr double MAX_SPEED_RATIO = 4.0;

    // ========================================================================
    // Helper: Wrap phase within [0, period)
    // ========================================================================

    // ========================================================================
    // Container Operations (clip-level only)
    // ========================================================================

    /**
     * @brief Move clip container to new timeline position
     * @param clip Clip to move
     * @param newStartTime New absolute timeline position (clamped to >= 0.0)
     */
    static inline void moveContainer(ClipInfo& clip, double newStartTime) {
        clip.startTime = juce::jmax(0.0, newStartTime);
    }

    /**
     * @brief Resize clip container from left edge
     *
     * TE-aligned behavior:
     * - Non-looped: adjusts offset so audio content stays at its timeline position
     * - Looped: adjusts offset (wrapped within loop region) so audio content stays at its timeline
     * position
     *
     * @param clip Clip to resize
     * @param newLength New clip length (clamped to >= MIN_CLIP_LENGTH)
     * @param bpm Current tempo (used if autoTempo is enabled)
     */
    static inline void resizeContainerFromLeft(ClipInfo& clip, double newLength,
                                               double bpm = 120.0) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = clip.length - newLength;
        double newStartTime = juce::jmax(0.0, clip.startTime + lengthDelta);
        double actualDelta = newStartTime - clip.startTime;

        // NOTE: In auto-tempo mode, do NOT update loopLengthBeats here.
        // loopLengthBeats is the authoritative source of truth and should only
        // be updated when the user explicitly changes it, not during tempo-driven resizes.

        if (clip.type == ClipType::Audio && !clip.audioFilePath.isEmpty()) {
            bool isAutoTempo = clip.isBeatsAuthoritative() && clip.sourceBPM > 0.0 && bpm > 0.0;

            if (isAutoTempo) {
                // Auto-tempo: work in beats (authoritative), derive seconds
                double deltaBeats = actualDelta * bpm / 60.0;
                if (!clip.loopEnabled) {
                    clip.offsetBeats = juce::jmax(0.0, clip.offsetBeats + deltaBeats);
                } else if (clip.loopLengthBeats > 0.0) {
                    double relBeats = clip.offsetBeats - clip.loopStartBeats;
                    clip.offsetBeats = clip.loopStartBeats +
                                       wrapPhase(relBeats + deltaBeats, clip.loopLengthBeats);
                }
                // Derive source-time seconds for paint/display
                clip.offset = clip.offsetBeats * 60.0 / clip.sourceBPM;
                if (!clip.loopEnabled)
                    clip.loopStart = clip.offset;
            } else {
                // Manual stretch: work in source-time seconds
                double toSource = clip.speedRatio;
                if (!clip.loopEnabled) {
                    double sourceDelta = actualDelta * toSource;
                    clip.offset = juce::jmax(0.0, clip.offset + sourceDelta);
                    clip.loopStart = clip.offset;
                } else {
                    double sourceLength =
                        clip.loopLength > 0.0 ? clip.loopLength : clip.length * toSource;
                    if (sourceLength > 0.0) {
                        double phaseDelta = actualDelta * toSource;
                        double relOffset = clip.offset - clip.loopStart;
                        clip.offset =
                            clip.loopStart + wrapPhase(relOffset + phaseDelta, sourceLength);
                    }
                }
            }
        } else if (clip.type == ClipType::MIDI) {
            // MIDI phase lives in midiOffset (beats). Do NOT touch clip.offset.
            double beatsPerSecond = bpm / 60.0;
            double deltaBeat = actualDelta * beatsPerSecond;
            if (clip.loopEnabled && clip.loopLengthBeats > 0.0) {
                // Looped: wrap midiOffset phase within loop for content alignment.
                // Piano roll is forced to relative mode for looped clips, so
                // midiTrimOffset is not needed.
                clip.midiOffset = wrapPhase(clip.midiOffset + deltaBeat, clip.loopLengthBeats);
            } else {
                // Non-looped: midiOffset stays unchanged (user-controlled).
                // midiTrimOffset tracks the cumulative left-resize delta (in beats) so the
                // piano roll (absolute mode) keeps notes at their timeline positions.
                // Positive = clip start moved right (shrunk), negative = moved left (expanded).
                clip.midiTrimOffset += deltaBeat;
            }
        }

        clip.startTime = newStartTime;
        clip.length = newLength;
        if (bpm > 0.0)
            clip.startBeats = newStartTime * bpm / 60.0;
        if (clip.isBeatsAuthoritative() && bpm > 0.0) {
            clip.lengthBeats = newLength * bpm / 60.0;
        }
    }

    /**
     * @brief Resize clip container from right edge
     *
     * For non-looped clips: loopLength tracks with clip length
     * For looped clips: only changes length (more/fewer loop cycles)
     *
     * @param clip Clip to resize
     * @param newLength New clip length (clamped to >= MIN_CLIP_LENGTH)
     * @param bpm Current tempo (used if autoTempo is enabled)
     */
    static inline void resizeContainerFromRight(ClipInfo& clip, double newLength,
                                                double bpm = 120.0) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        clip.length = newLength;
        if (clip.isBeatsAuthoritative() && bpm > 0.0) {
            clip.lengthBeats = newLength * bpm / 60.0;
        }
    }

    // ========================================================================
    // Audio Operations (clip-level fields)
    // ========================================================================

    /**
     * @brief Trim audio from left edge
     * Adjusts clip.offset, clip.startTime, clip.length.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromLeft(ClipInfo& clip, double trimAmount,
                                         double fileDuration = 0.0, double bpm = 0.0) {
        double sourceDelta = trimAmount * clip.speedRatio;
        double newOffset = clip.offset + sourceDelta;

        if (fileDuration > 0.0) {
            newOffset = juce::jmin(newOffset, fileDuration);
        }
        newOffset = juce::jmax(0.0, newOffset);

        double actualSourceDelta = newOffset - clip.offset;
        double timelineDelta = actualSourceDelta / clip.speedRatio;

        clip.offset = newOffset;
        clip.loopStart = clip.offset;
        clip.startTime = juce::jmax(0.0, clip.startTime + timelineDelta);
        clip.length = juce::jmax(MIN_CLIP_LENGTH, clip.length - timelineDelta);

        if (bpm > 0.0)
            clip.startBeats = clip.startTime * bpm / 60.0;
        if (clip.isBeatsAuthoritative() && bpm > 0.0) {
            clip.lengthBeats = clip.length * bpm / 60.0;
        }
    }

    /**
     * @brief Trim audio from right edge
     * Adjusts clip.length and loopLength.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromRight(ClipInfo& clip, double trimAmount,
                                          double fileDuration = 0.0, double bpm = 0.0) {
        double newLength = clip.length - trimAmount;

        if (fileDuration > 0.0) {
            double maxLength = (fileDuration - clip.offset) / clip.speedRatio;
            newLength = juce::jmin(newLength, maxLength);
        }

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        clip.length = newLength;

        if (clip.isBeatsAuthoritative() && bpm > 0.0) {
            clip.lengthBeats = newLength * bpm / 60.0;
        }
    }

    /**
     * @brief Stretch audio from right edge
     * Adjusts clip.length and clip.speedRatio.
     * @param clip Clip to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    static inline void stretchAudioFromRight(ClipInfo& clip, double newLength, double oldLength,
                                             double originalSpeedRatio) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

        double stretchRatio = newLength / oldLength;
        double newSpeedRatio = originalSpeedRatio / stretchRatio;
        newSpeedRatio = juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, newSpeedRatio);

        newLength = oldLength * (originalSpeedRatio / newSpeedRatio);

        clip.length = newLength;
        clip.speedRatio = newSpeedRatio;

        // Keep loopLength in sync for non-looped clips
        if (!clip.loopEnabled)
            clip.loopLength = clip.timelineToSource(clip.length);
    }

    /**
     * @brief Stretch audio from left edge
     * Adjusts clip.startTime, clip.length, clip.speedRatio to keep right edge fixed.
     * @param clip Clip to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    static inline void stretchAudioFromLeft(ClipInfo& clip, double newLength, double oldLength,
                                            double originalSpeedRatio) {
        double rightEdge = clip.startTime + clip.length;

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

        double stretchRatio = newLength / oldLength;
        double newSpeedRatio = originalSpeedRatio / stretchRatio;
        newSpeedRatio = juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, newSpeedRatio);

        newLength = oldLength * (originalSpeedRatio / newSpeedRatio);

        clip.length = newLength;
        clip.startTime = rightEdge - newLength;
        clip.speedRatio = newSpeedRatio;

        // Keep loopLength in sync for non-looped clips
        if (!clip.loopEnabled)
            clip.loopLength = clip.timelineToSource(clip.length);
    }

    // ========================================================================
    // Compound Operations (container + content)
    // ========================================================================

    /**
     * @brief Stretch clip from left edge (arrangement-level operation)
     * Resizes container from left AND stretches audio proportionally.
     * @param clip Clip to stretch
     * @param newLength New clip length
     */
    static inline void stretchClipFromLeft(ClipInfo& clip, double newLength) {
        if (clip.type != ClipType::Audio || clip.audioFilePath.isEmpty()) {
            resizeContainerFromLeft(clip, newLength);
            return;
        }

        double oldLength = clip.length;
        double originalSpeedRatio = clip.speedRatio;

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = clip.length - newLength;
        clip.startTime = juce::jmax(0.0, clip.startTime + lengthDelta);
        clip.length = newLength;

        // Stretch audio proportionally
        stretchAudioFromLeft(clip, newLength, oldLength, originalSpeedRatio);
    }

    /**
     * @brief Stretch clip from right edge (arrangement-level operation)
     * Resizes container from right AND stretches audio proportionally.
     * @param clip Clip to stretch
     * @param newLength New clip length
     */
    static inline void stretchClipFromRight(ClipInfo& clip, double newLength) {
        if (clip.type != ClipType::Audio || clip.audioFilePath.isEmpty()) {
            resizeContainerFromRight(clip, newLength);
            return;
        }

        double oldLength = clip.length;
        double originalSpeedRatio = clip.speedRatio;

        resizeContainerFromRight(clip, newLength);

        stretchAudioFromRight(clip, newLength, oldLength, originalSpeedRatio);
    }

    // ========================================================================
    // Arrangement Drag Helpers (absolute target state)
    // ========================================================================

    /**
     * @brief Resize container to absolute target start/length (for drag preview).
     * Maintains loopLength invariant for non-looped clips.
     * @param clip Clip to resize
     * @param newStartTime New start time
     * @param newLength New clip length
     */
    static inline void resizeContainerAbsolute(ClipInfo& clip, double newStartTime,
                                               double newLength) {
        clip.startTime = newStartTime;
        resizeContainerFromRight(clip, newLength);
    }

    /**
     * @brief Update autoTempo clip for a new total beat count (stretch).
     * Adjusts sourceBPM so TE stretches the same source audio to fill newBeats.
     * Mirrors ClipManager::setLengthBeats logic.
     */
    static inline void stretchAutoTempoBeats(ClipInfo& clip, double newTotalBeats, double /*bpm*/) {
        double sourceSeconds = clip.loopLength > 0.0 ? clip.loopLength : clip.getSourceLength();
        if (sourceSeconds <= 0.0)
            return;

        // Compute stretch ratio from total beats (handles multiple loop cycles)
        double oldTotalBeats = clip.lengthBeats > 0.0 ? clip.lengthBeats : clip.loopLengthBeats;
        if (oldTotalBeats <= 0.0)
            return;

        double stretchRatio = newTotalBeats / oldTotalBeats;

        // Scale per-cycle beats proportionally
        double newLoopBeats = clip.loopLengthBeats * stretchRatio;

        // Update sourceBPM from new per-cycle beats
        double oldSourceBPM = clip.sourceBPM;
        clip.sourceBPM = newLoopBeats * 60.0 / sourceSeconds;

        if (oldSourceBPM > 0.0 && clip.sourceNumBeats > 0.0) {
            clip.sourceNumBeats *= clip.sourceBPM / oldSourceBPM;
        }

        clip.loopLengthBeats = newLoopBeats;
        clip.lengthBeats = newTotalBeats;
        clip.loopStartBeats = clip.loopStart * clip.sourceBPM / 60.0;
    }

    /**
     * @brief Stretch to absolute target speed/length (for drag preview).
     * For autoTempo clips, changes lengthBeats instead of speedRatio.
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio (ignored for autoTempo)
     * @param newLength New clip length in seconds
     * @param bpm Current project tempo
     */
    static inline void stretchAbsolute(ClipInfo& clip, double newSpeedRatio, double newLength,
                                       double bpm = 120.0) {
        clip.length = newLength;
        if (clip.autoTempo && bpm > 0.0) {
            double newBeats = newLength * bpm / 60.0;
            stretchAutoTempoBeats(clip, newBeats, bpm);
        } else {
            clip.speedRatio = newSpeedRatio;
        }
    }

    /**
     * @brief Stretch from left edge to absolute target (for drag preview).
     * Keeps right edge fixed. For autoTempo clips, changes lengthBeats.
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio (ignored for autoTempo)
     * @param newLength New clip length in seconds
     * @param rightEdge Fixed right edge position
     * @param bpm Current project tempo
     */
    static inline void stretchAbsoluteFromLeft(ClipInfo& clip, double newSpeedRatio,
                                               double newLength, double rightEdge,
                                               double bpm = 120.0) {
        clip.length = newLength;
        clip.startTime = rightEdge - newLength;
        if (clip.autoTempo && bpm > 0.0) {
            double newBeats = newLength * bpm / 60.0;
            stretchAutoTempoBeats(clip, newBeats, bpm);
        } else {
            clip.speedRatio = newSpeedRatio;
        }
    }

    /**
     * @brief Scale MIDI notes proportionally when stretching a MIDI clip.
     * @param clip Clip whose midiNotes to scale
     * @param stretchRatio Ratio of newLength / oldLength (>1 = longer, <1 = shorter)
     */
    static inline void stretchMidiNotes(ClipInfo& clip, double stretchRatio) {
        for (auto& note : clip.midiNotes) {
            note.startBeat *= stretchRatio;
            note.lengthBeats *= stretchRatio;
        }
    }

    // ========================================================================
    // Auto-Tempo Operations (Musical Mode)
    // ========================================================================

    /**
     * @brief Calculate the beat-based loop range for Tracktion Engine sync
     *
     * Converts model beat values (project beats) to SOURCE beats for TE.
     * TE's loopStartBeats/loopLengthBeats are in source-file beats (clamped
     * to loopInfo.getNumBeats()), NOT project-timeline beats.
     *
     * @param clip The clip to calculate for
     * @param bpm Current project tempo
     * @return Pair of (loopStartBeats, loopLengthBeats) in SOURCE beats
     */
    static inline std::pair<double, double> getAutoTempoBeatRange(const ClipInfo& clip,
                                                                  double bpm) {
        if (!clip.autoTempo && !clip.warpEnabled) {
            return {0.0, 0.0};
        }

        // Use stored beat values when available (set by setAutoTempo / setClipBeats)
        if (clip.loopLengthBeats > 0.0) {
            double start = clip.loopStartBeats;
            double length = clip.loopLengthBeats;
            // Clamp to sourceNumBeats (TE can't read beyond the file)
            if (clip.sourceNumBeats > 0.0) {
                if (length > clip.sourceNumBeats) {
                    length = clip.sourceNumBeats;
                    start = 0.0;
                } else if (start + length > clip.sourceNumBeats) {
                    start = clip.sourceNumBeats - length;
                    if (start < 0.0)
                        start = 0.0;
                }
            }
            return {start, length};
        }

        // Derive from source-time seconds using sourceBPM
        if (clip.sourceBPM > 0.0) {
            double srcBps = clip.sourceBPM / 60.0;
            double start = clip.loopStart * srcBps;
            double length = clip.loopLength * srcBps;

            // TE's setLoopRangeBeats clamps end to loopInfo.getNumBeats().
            // In time-based mode loops can wrap past file end, but beat-based
            // mode cannot. Shift the start back so the full region fits.
            if (clip.sourceNumBeats > 0.0) {
                if (length > clip.sourceNumBeats) {
                    length = clip.sourceNumBeats;
                    start = 0.0;
                } else if (start + length > clip.sourceNumBeats) {
                    start = clip.sourceNumBeats - length;
                    if (start < 0.0)
                        start = 0.0;
                }
            }

            return {start, length};
        }

        // Fallback: return project beats (correct only when project BPM == source BPM)
        return {clip.loopStartBeats, clip.loopLengthBeats};
    }

    /**
     * @brief Set clip to use beat-based length (enables autoTempo, stores beat values)
     * @param clip Clip to modify
     * @param lengthBeats Clip length in beats
     * @param loopStartBeats Loop start position in beats (relative to file start)
     * @param loopLengthBeats Loop length in beats (0 = derive from clip length)
     * @param bpm Current tempo for time conversion
     */
    static inline void setClipLengthBeats(ClipInfo& clip, double lengthBeats, double loopStartBeats,
                                          double loopLengthBeats, double bpm) {
        clip.autoTempo = true;
        clip.lengthBeats = lengthBeats;
        clip.loopLengthBeats = loopLengthBeats > 0.0 ? loopLengthBeats : lengthBeats;
        clip.loopStartBeats = loopStartBeats;

        // Update time-based fields (derived values)
        clip.setLengthFromBeats(lengthBeats, bpm);

        // Auto-tempo requires speedRatio=1.0
        clip.speedRatio = 1.0;
    }

    /**
     * @brief Toggle auto-tempo mode (converts between time↔beat storage)
     * @param clip Clip to modify
     * @param enabled Enable auto-tempo mode
     * @param bpm Current tempo for conversion
     */
    static inline void setAutoTempo(ClipInfo& clip, bool enabled, double bpm) {
        if (clip.autoTempo == enabled)
            return;

        if (enabled && bpm <= 0.0)
            return;

        clip.autoTempo = enabled;

        if (enabled) {
            clip.analogPitch = false;  // Analog pitch is incompatible with autoTempo

            // Convert current offset to beats
            if (clip.sourceBPM > 0.0)
                clip.offsetBeats = clip.offset * clip.sourceBPM / 60.0;

            // Convert current timeline position to beats
            clip.startBeats = (clip.startTime * bpm) / 60.0;

            // Enable looping (required for TE's autoTempo beat range to work)
            if (!clip.loopEnabled) {
                clip.loopEnabled = true;
                clip.loopStart = clip.offset;
                clip.setLoopLengthFromTimeline(clip.length);
            }

            // Use source file's beat count when available, otherwise derive from
            // sourceBPM and source duration, falling back to project BPM
            double sourceBeats = clip.sourceNumBeats;
            if (sourceBeats <= 0.0 && clip.sourceBPM > 0.0) {
                double sourceDuration = clip.getSourceLength();
                sourceBeats = sourceDuration * clip.sourceBPM / 60.0;
            }
            clip.lengthBeats = sourceBeats > 0.0 ? sourceBeats : clip.getLengthInBeats(bpm);

            if (clip.loopEnabled && clip.loopLength > 0.0) {
                clip.loopLengthBeats =
                    sourceBeats > 0.0 ? sourceBeats : (clip.loopLength * bpm) / 60.0;
                clip.loopStartBeats = (clip.loopStart * bpm) / 60.0;
            } else {
                clip.loopLengthBeats = clip.lengthBeats;
                clip.loopStartBeats = 0.0;
            }

            // Calibrate sourceBPM to the current playback speed so that enabling
            // autoTempo doesn't change the audible playback speed — but only when
            // sourceBPM matches the project BPM (i.e., was defaulted, not detected).
            // When sourceBPM is from real BPM detection, preserve it so TE applies
            // the correct stretch ratio (projectBPM / sourceBPM).
            double effectiveBPM = bpm / clip.speedRatio;
            if (std::abs(clip.sourceBPM - effectiveBPM) < 0.1) {
                if (clip.sourceBPM > 0.0 && clip.sourceNumBeats > 0.0) {
                    double fileDuration = clip.sourceNumBeats * 60.0 / clip.sourceBPM;
                    clip.sourceNumBeats = effectiveBPM * fileDuration / 60.0;
                }
                clip.sourceBPM = effectiveBPM;
            }

            // Force speedRatio to 1.0 (TE requirement for autoTempo)
            clip.speedRatio = 1.0;
        } else {
            // Switching to time-based mode: keep current derived time values
            // Clear beat values (no longer used)
            clip.startBeats = -1.0;
            clip.loopStartBeats = 0.0;
            clip.loopLengthBeats = 0.0;
            clip.lengthBeats = 0.0;
        }
    }

    /**
     * @brief Resize clip from right edge in musical mode (beat-based)
     * @param clip Clip to resize
     * @param newLengthBeats New length in beats
     * @param bpm Current tempo for time conversion
     */
    static inline void resizeClipFromRightMusical(ClipInfo& clip, double newLengthBeats,
                                                  double bpm) {
        newLengthBeats = juce::jmax(MIN_CLIP_LENGTH * bpm / 60.0, newLengthBeats);

        clip.lengthBeats = newLengthBeats;
        clip.setLengthFromBeats(newLengthBeats, bpm);
    }

    /**
     * @brief Resize clip from left edge in musical mode (beat-based)
     * @param clip Clip to resize
     * @param newLengthBeats New length in beats
     * @param bpm Current tempo for time conversion
     */
    static inline void resizeClipFromLeftMusical(ClipInfo& clip, double newLengthBeats,
                                                 double bpm) {
        newLengthBeats = juce::jmax(MIN_CLIP_LENGTH * bpm / 60.0, newLengthBeats);

        double oldLength = clip.length;
        clip.lengthBeats = newLengthBeats;
        clip.setLengthFromBeats(newLengthBeats, bpm);

        // Adjust startTime to keep right edge fixed
        double lengthDelta = oldLength - clip.length;
        clip.startTime = juce::jmax(0.0, clip.startTime + lengthDelta);
    }

    // ========================================================================
    // Editor-Specific Operations
    // ========================================================================

    /**
     * @brief Move loop start (editor left-edge drag in loop mode)
     * @param clip Clip to modify
     * @param newLoopStart New loop start position in source time
     * @param fileDuration Total file duration for clamping
     */
    static inline void moveLoopStart(ClipInfo& clip, double newLoopStart, double fileDuration) {
        double oldLoopLength = clip.loopLength;
        clip.loopStart = newLoopStart;
        // Clamp loopLength to available audio from new loopStart
        if (fileDuration > 0.0) {
            double avail = fileDuration - clip.loopStart;
            if (clip.loopLength > avail) {
                clip.loopLength = juce::jmax(0.0, avail);
                if (clip.isBeatsAuthoritative() && oldLoopLength > 0.0) {
                    clip.loopLengthBeats *= clip.loopLength / oldLoopLength;
                }
            }
        }
        clip.clampLengthToSource(fileDuration);
    }

    /**
     * @brief Set source extent via timeline extent (editor right-edge drag)
     * Updates loopLength from timeline extent.
     * For non-looped clips, also updates clip.length.
     * @param clip Clip to modify
     * @param newTimelineExtent New extent in timeline seconds
     */
    static inline void resizeSourceExtent(ClipInfo& clip, double newTimelineExtent) {
        clip.setLoopLengthFromTimeline(newTimelineExtent);
        if (!clip.loopEnabled) {
            clip.length = newTimelineExtent;
        }
    }

    /**
     * @brief Stretch in editor (changes speedRatio, scales clip.length,
     * adjusts loopLength for looped clips)
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio
     * @param clipLengthScaleFactor Ratio of new speed to original speed (newSpeedRatio /
     * dragStartSpeedRatio)
     * @param dragStartClipLength Original clip length at drag start
     * @param dragStartExtent Source extent in timeline seconds at drag start (for loopLength calc)
     */
    static inline void stretchEditor(ClipInfo& clip, double newSpeedRatio,
                                     double clipLengthScaleFactor, double dragStartClipLength,
                                     double dragStartExtent) {
        clip.speedRatio = newSpeedRatio;
        clip.length = dragStartClipLength * clipLengthScaleFactor;
        // In loop mode, adjust loopLength to keep loop markers fixed on timeline
        if (clip.loopEnabled && clip.loopLength > 0.0) {
            clip.loopLength = dragStartExtent / newSpeedRatio;
        }
    }

    /**
     * @brief Stretch from left in editor (also adjusts startTime)
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio
     * @param clipLengthScaleFactor Ratio of new speed to original speed (newSpeedRatio /
     * dragStartSpeedRatio)
     * @param dragStartClipLength Original clip length at drag start
     * @param dragStartExtent Source extent in timeline seconds at drag start (for loopLength calc)
     * @param rightEdge Fixed right edge position (dragStartStartTime + dragStartClipLength)
     */
    static inline void stretchEditorFromLeft(ClipInfo& clip, double newSpeedRatio,
                                             double clipLengthScaleFactor,
                                             double dragStartClipLength, double dragStartExtent,
                                             double rightEdge) {
        clip.speedRatio = newSpeedRatio;
        clip.length = dragStartClipLength * clipLengthScaleFactor;
        clip.startTime = rightEdge - clip.length;
        // In loop mode, adjust loopLength to keep loop markers fixed on timeline
        if (clip.loopEnabled && clip.loopLength > 0.0) {
            clip.loopLength = dragStartExtent / newSpeedRatio;
        }
    }

    // =========================================================================
    // MIDI Flatten (render loops/offsets into flat note list)
    // =========================================================================

    /**
     * @brief Flatten a MIDI clip's notes, expanding loops and applying offsets.
     *
     * Looped: repeats notes for each loop cycle across lengthBeats, applying midiOffset phase.
     * Non-looped: shifts notes by -midiTrimOffset, clips to 0..lengthBeats.
     * After flattening, looping is disabled and offsets are reset to 0.
     */
    static inline void flattenMidiClip(ClipInfo& clip) {
        if (clip.type != ClipType::MIDI)
            return;

        std::vector<MidiNote> flatNotes;
        double clipLen = clip.lengthBeats;

        if (clip.loopEnabled && clip.loopLengthBeats > 0.0) {
            double loopLen = clip.loopLengthBeats;
            double phase = clip.midiOffset;

            // Number of full loop cycles that fit in the clip
            int numCycles = static_cast<int>(std::ceil(clipLen / loopLen));

            for (int cycle = 0; cycle < numCycles; ++cycle) {
                double cycleStart = cycle * loopLen - phase;

                for (const auto& note : clip.midiNotes) {
                    // Only include notes within the loop region
                    if (note.startBeat >= loopLen || note.startBeat + note.lengthBeats <= 0.0)
                        continue;

                    double noteStart = cycleStart + note.startBeat;
                    double noteLen = note.lengthBeats;

                    // Clip note to loop boundary
                    if (note.startBeat + noteLen > loopLen)
                        noteLen = loopLen - note.startBeat;

                    // Skip notes entirely outside clip range
                    if (noteStart + noteLen <= 0.0 || noteStart >= clipLen)
                        continue;

                    // Trim to clip boundaries
                    if (noteStart < 0.0) {
                        noteLen += noteStart;
                        noteStart = 0.0;
                    }
                    if (noteStart + noteLen > clipLen)
                        noteLen = clipLen - noteStart;

                    if (noteLen > 0.0) {
                        MidiNote flat = note;
                        flat.startBeat = noteStart;
                        flat.lengthBeats = noteLen;
                        flatNotes.push_back(flat);
                    }
                }
            }

            // Flatten CC data
            std::vector<MidiCCData> flatCC;
            for (int cycle = 0; cycle < numCycles; ++cycle) {
                double cycleStart = cycle * loopLen - phase;
                for (const auto& cc : clip.midiCCData) {
                    if (cc.beatPosition >= loopLen)
                        continue;
                    double pos = cycleStart + cc.beatPosition;
                    if (pos < 0.0 || pos >= clipLen)
                        continue;
                    MidiCCData flat = cc;
                    flat.beatPosition = pos;
                    flatCC.push_back(flat);
                }
            }
            clip.midiCCData = std::move(flatCC);

            // Flatten pitch bend data
            std::vector<MidiPitchBendData> flatPB;
            for (int cycle = 0; cycle < numCycles; ++cycle) {
                double cycleStart = cycle * loopLen - phase;
                for (const auto& pb : clip.midiPitchBendData) {
                    if (pb.beatPosition >= loopLen)
                        continue;
                    double pos = cycleStart + pb.beatPosition;
                    if (pos < 0.0 || pos >= clipLen)
                        continue;
                    MidiPitchBendData flat = pb;
                    flat.beatPosition = pos;
                    flatPB.push_back(flat);
                }
            }
            clip.midiPitchBendData = std::move(flatPB);

            clip.loopEnabled = false;
            clip.midiOffset = 0.0;
            clip.loopLengthBeats = 0.0;
            clip.loopLength = 0.0;
            clip.loopStart = 0.0;
            clip.loopStartBeats = 0.0;
        } else {
            // Non-looped: apply midiTrimOffset
            double trimOffset = clip.midiTrimOffset;

            for (const auto& note : clip.midiNotes) {
                double noteStart = note.startBeat - trimOffset;
                double noteLen = note.lengthBeats;

                // Skip notes entirely outside clip range
                if (noteStart + noteLen <= 0.0 || noteStart >= clipLen)
                    continue;

                // Trim to clip boundaries
                if (noteStart < 0.0) {
                    noteLen += noteStart;
                    noteStart = 0.0;
                }
                if (noteStart + noteLen > clipLen)
                    noteLen = clipLen - noteStart;

                if (noteLen > 0.0) {
                    MidiNote flat = note;
                    flat.startBeat = noteStart;
                    flat.lengthBeats = noteLen;
                    flatNotes.push_back(flat);
                }
            }

            // Apply trim to CC data
            std::vector<MidiCCData> flatCC;
            for (const auto& cc : clip.midiCCData) {
                double pos = cc.beatPosition - trimOffset;
                if (pos < 0.0 || pos >= clipLen)
                    continue;
                MidiCCData flat = cc;
                flat.beatPosition = pos;
                flatCC.push_back(flat);
            }
            clip.midiCCData = std::move(flatCC);

            // Apply trim to pitch bend data
            std::vector<MidiPitchBendData> flatPB;
            for (const auto& pb : clip.midiPitchBendData) {
                double pos = pb.beatPosition - trimOffset;
                if (pos < 0.0 || pos >= clipLen)
                    continue;
                MidiPitchBendData flat = pb;
                flat.beatPosition = pos;
                flatPB.push_back(flat);
            }
            clip.midiPitchBendData = std::move(flatPB);

            clip.midiTrimOffset = 0.0;
        }

        clip.midiNotes = std::move(flatNotes);
    }

  private:
    ClipOperations() = delete;  // Static class, no instances
};

}  // namespace magda
