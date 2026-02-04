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

    static inline double wrapPhase(double value, double period) {
        if (period <= 0.0)
            return 0.0;
        double result = std::fmod(value, period);
        if (result < 0.0)
            result += period;
        return result;
    }

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
            if (!clip.loopEnabled) {
                // Non-looped: adjust offset so content stays at timeline position
                double sourceDelta = actualDelta * clip.speedRatio;
                clip.offset = juce::jmax(0.0, clip.offset + sourceDelta);
            } else {
                // Looped: adjust offset (wrapped within loop region) so content stays at timeline
                // position
                double sourceLength =
                    clip.loopLength > 0.0 ? clip.loopLength : clip.length * clip.speedRatio;
                if (sourceLength > 0.0) {
                    double phaseDelta = actualDelta * clip.speedRatio;
                    double relOffset = clip.offset - clip.loopStart;
                    clip.offset = clip.loopStart + wrapPhase(relOffset + phaseDelta, sourceLength);
                }
            }
        }

        clip.startTime = newStartTime;
        clip.length = newLength;
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

        // NOTE: In auto-tempo mode, do NOT update loopLengthBeats here.
        // loopLengthBeats is the authoritative source of truth and should only
        // be updated when the user explicitly changes it, not during tempo-driven resizes.

        // For non-looped audio clips, update loopLength to track (used for display)
        if (clip.type == ClipType::Audio && !clip.audioFilePath.isEmpty() && !clip.loopEnabled) {
            clip.setLoopLengthFromTimeline(newLength);
        }

        clip.length = newLength;
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
                                         double fileDuration = 0.0) {
        double sourceDelta = trimAmount * clip.speedRatio;
        double newOffset = clip.offset + sourceDelta;

        if (fileDuration > 0.0) {
            newOffset = juce::jmin(newOffset, fileDuration);
        }
        newOffset = juce::jmax(0.0, newOffset);

        double actualSourceDelta = newOffset - clip.offset;
        double timelineDelta = actualSourceDelta / clip.speedRatio;

        clip.offset = newOffset;
        clip.startTime = juce::jmax(0.0, clip.startTime + timelineDelta);
        clip.length = juce::jmax(MIN_CLIP_LENGTH, clip.length - timelineDelta);
    }

    /**
     * @brief Trim audio from right edge
     * Adjusts clip.length and loopLength.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromRight(ClipInfo& clip, double trimAmount,
                                          double fileDuration = 0.0) {
        double newLength = clip.length - trimAmount;

        if (fileDuration > 0.0) {
            double maxLength = (fileDuration - clip.offset) / clip.speedRatio;
            newLength = juce::jmin(newLength, maxLength);
        }

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        clip.length = newLength;

        // Update loopLength to track for non-looped clips
        if (!clip.loopEnabled) {
            clip.setLoopLengthFromTimeline(newLength);
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
     * @brief Stretch to absolute target speed/length (for drag preview).
     * Maintains loopLength when looped (keeps loop markers fixed on timeline).
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio
     * @param newLength New clip length
     */
    static inline void stretchAbsolute(ClipInfo& clip, double newSpeedRatio, double newLength) {
        clip.speedRatio = newSpeedRatio;
        clip.length = newLength;

        // For non-looped clips, loopLength tracks with clip length
        if (!clip.loopEnabled) {
            clip.setLoopLengthFromTimeline(newLength);
        }
    }

    /**
     * @brief Stretch from left edge to absolute target (for drag preview).
     * Keeps right edge fixed.
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio
     * @param newLength New clip length
     * @param rightEdge Fixed right edge position
     */
    static inline void stretchAbsoluteFromLeft(ClipInfo& clip, double newSpeedRatio,
                                               double newLength, double rightEdge) {
        clip.speedRatio = newSpeedRatio;
        clip.length = newLength;
        clip.startTime = rightEdge - newLength;

        // For non-looped clips, loopLength tracks with clip length
        if (!clip.loopEnabled) {
            clip.setLoopLengthFromTimeline(newLength);
        }
    }

    // ========================================================================
    // Auto-Tempo Operations (Musical Mode)
    // ========================================================================

    /**
     * @brief Calculate the beat-based loop range for auto-tempo mode
     *
     * Returns the loop range in beats that should be sent to Tracktion Engine.
     * This defines how much SOURCE audio is available to loop, not the clip's timeline length.
     *
     * @param clip The clip to calculate for
     * @param bpm Current tempo
     * @return Pair of (loopStartBeats, loopLengthBeats)
     */
    static inline std::pair<double, double> getAutoTempoBeatRange(const ClipInfo& clip,
                                                                  double bpm) {
        if (!clip.autoTempo) {
            return {0.0, 0.0};
        }

        // In musical mode, beat values are authoritative - just return the stored values
        // DO NOT recalculate from time, as that defeats the purpose of tempo-locked beats
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

        clip.autoTempo = enabled;

        if (enabled) {
            // Convert current timeline position to beats
            clip.startBeats = (clip.startTime * bpm) / 60.0;

            // Convert current time length to beats
            clip.loopLengthBeats = clip.getLengthInBeats(bpm);
            if (clip.loopEnabled && clip.loopLength > 0.0) {
                // Convert loop properties to beats
                clip.loopStartBeats = (clip.loopStart * bpm) / 60.0;
            } else {
                clip.loopStartBeats = 0.0;
            }

            // Enable looping (required for TE's autoTempo beat range to work)
            if (!clip.loopEnabled) {
                clip.loopEnabled = true;
                // Set loop start to offset and loop length from clip length
                clip.loopStart = clip.offset;
                clip.setLoopLengthFromTimeline(clip.length);
            }

            // Force speedRatio to 1.0 (TE requirement for autoTempo)
            clip.speedRatio = 1.0;
        } else {
            // Switching to time-based mode: keep current derived time values
            // Clear beat values (no longer used)
            clip.startBeats = -1.0;
            clip.loopStartBeats = 0.0;
            clip.loopLengthBeats = 0.0;
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

        clip.loopLengthBeats = newLengthBeats;
        clip.setLengthFromBeats(newLengthBeats, bpm);

        // Update loopLength for display consistency
        if (!clip.loopEnabled) {
            clip.setLoopLengthFromTimeline(clip.length);
        }
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
        clip.loopLengthBeats = newLengthBeats;
        clip.setLengthFromBeats(newLengthBeats, bpm);

        // Adjust startTime to keep right edge fixed
        double lengthDelta = oldLength - clip.length;
        clip.startTime = juce::jmax(0.0, clip.startTime + lengthDelta);

        // Update loopLength for display consistency
        if (!clip.loopEnabled) {
            clip.setLoopLengthFromTimeline(clip.length);
        }
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
        clip.loopStart = newLoopStart;
        // Clamp loopLength to available audio from new loopStart
        if (fileDuration > 0.0) {
            double avail = fileDuration - clip.loopStart;
            if (clip.loopLength > avail)
                clip.loopLength = juce::jmax(0.0, avail);
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

  private:
    ClipOperations() = delete;  // Static class, no instances
};

}  // namespace magda
