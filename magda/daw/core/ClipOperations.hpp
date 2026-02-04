#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

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
     * @param bpm Current tempo (unused, kept for API compatibility)
     */
    static inline void resizeContainerFromLeft(ClipInfo& clip, double newLength,
                                               double bpm = 120.0) {
        juce::ignoreUnused(bpm);
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = clip.length - newLength;
        double newStartTime = juce::jmax(0.0, clip.startTime + lengthDelta);
        double actualDelta = newStartTime - clip.startTime;

        if (clip.type == ClipType::Audio && !clip.audioFilePath.isEmpty()) {
            if (!clip.loopEnabled) {
                // Non-looped: adjust offset so content stays at timeline position
                double sourceDelta = actualDelta / clip.speedRatio;
                clip.offset = juce::jmax(0.0, clip.offset + sourceDelta);
            } else {
                // Looped: adjust offset (wrapped within loop region) so content stays at timeline
                // position
                double sourceLength =
                    clip.loopLength > 0.0 ? clip.loopLength : clip.length / clip.speedRatio;
                if (sourceLength > 0.0) {
                    double phaseDelta = actualDelta / clip.speedRatio;
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
     */
    static inline void resizeContainerFromRight(ClipInfo& clip, double newLength) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

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
        double sourceDelta = trimAmount / clip.speedRatio;
        double newOffset = clip.offset + sourceDelta;

        if (fileDuration > 0.0) {
            newOffset = juce::jmin(newOffset, fileDuration);
        }
        newOffset = juce::jmax(0.0, newOffset);

        double actualSourceDelta = newOffset - clip.offset;
        double timelineDelta = actualSourceDelta * clip.speedRatio;

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
            double maxLength = (fileDuration - clip.offset) * clip.speedRatio;
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
        double newSpeedRatio = originalSpeedRatio * stretchRatio;
        newSpeedRatio = juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, newSpeedRatio);

        newLength = oldLength * (newSpeedRatio / originalSpeedRatio);

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
        double newSpeedRatio = originalSpeedRatio * stretchRatio;
        newSpeedRatio = juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, newSpeedRatio);

        newLength = oldLength * (newSpeedRatio / originalSpeedRatio);

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
