#pragma once

#include <juce_core/juce_core.h>

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
 * All methods are stateless and modify data structures in place.
 */
class ClipOperations {
  public:
    // ========================================================================
    // Constraint Constants
    // ========================================================================

    static constexpr double MIN_CLIP_LENGTH = 0.1;
    static constexpr double MIN_LOOP_LENGTH_BEATS = 0.25;
    static constexpr double MIN_STRETCH_FACTOR = 0.25;
    static constexpr double MAX_STRETCH_FACTOR = 4.0;

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
     * Moves clip start time, adjusts length. For audio clips, adjusts audioOffset.
     * @param clip Clip to resize
     * @param newLength New clip length (clamped to >= MIN_CLIP_LENGTH)
     */
    static inline void resizeContainerFromLeft(ClipInfo& clip, double newLength) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = clip.length - newLength;
        double newStartTime = juce::jmax(0.0, clip.startTime + lengthDelta);

        // For audio clips, adjust offset so the audible content stays aligned
        if (clip.type == ClipType::Audio && !clip.audioFilePath.isEmpty()) {
            double actualDelta = newStartTime - clip.startTime;
            double fileDelta = actualDelta / clip.audioStretchFactor;
            clip.audioOffset = juce::jmax(0.0, clip.audioOffset + fileDelta);
        }

        clip.startTime = newStartTime;
        clip.length = newLength;
    }

    /**
     * @brief Resize clip container from right edge
     * Only changes length, start time unchanged.
     * @param clip Clip to resize
     * @param newLength New clip length (clamped to >= MIN_CLIP_LENGTH)
     */
    static inline void resizeContainerFromRight(ClipInfo& clip, double newLength) {
        clip.length = juce::jmax(MIN_CLIP_LENGTH, newLength);
    }

    // ========================================================================
    // Audio Operations (clip-level fields)
    // ========================================================================

    /**
     * @brief Trim audio from left edge
     * Adjusts clip.audioOffset, clip.startTime, clip.length.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromLeft(ClipInfo& clip, double trimAmount,
                                         double fileDuration = 0.0) {
        double fileDelta = trimAmount / clip.audioStretchFactor;
        double newOffset = clip.audioOffset + fileDelta;

        if (fileDuration > 0.0) {
            newOffset = juce::jmin(newOffset, fileDuration);
        }
        newOffset = juce::jmax(0.0, newOffset);

        double actualFileDelta = newOffset - clip.audioOffset;
        double timelineDelta = actualFileDelta * clip.audioStretchFactor;

        clip.audioOffset = newOffset;
        clip.startTime = juce::jmax(0.0, clip.startTime + timelineDelta);
        clip.length = juce::jmax(MIN_CLIP_LENGTH, clip.length - timelineDelta);
    }

    /**
     * @brief Trim audio from right edge
     * Adjusts clip.length only, offset unchanged.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromRight(ClipInfo& clip, double trimAmount,
                                          double fileDuration = 0.0) {
        double newLength = clip.length - trimAmount;

        if (fileDuration > 0.0) {
            double maxLength = (fileDuration - clip.audioOffset) * clip.audioStretchFactor;
            newLength = juce::jmin(newLength, maxLength);
        }

        clip.length = juce::jmax(MIN_CLIP_LENGTH, newLength);
    }

    /**
     * @brief Stretch audio from right edge
     * Adjusts clip.length and clip.audioStretchFactor.
     * @param clip Clip to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalStretchFactor Original stretch factor at drag start
     */
    static inline void stretchAudioFromRight(ClipInfo& clip, double newLength, double oldLength,
                                             double originalStretchFactor) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

        double stretchRatio = newLength / oldLength;
        double newStretchFactor = originalStretchFactor * stretchRatio;
        newStretchFactor = juce::jlimit(MIN_STRETCH_FACTOR, MAX_STRETCH_FACTOR, newStretchFactor);

        newLength = oldLength * (newStretchFactor / originalStretchFactor);

        clip.length = newLength;
        clip.audioStretchFactor = newStretchFactor;
    }

    /**
     * @brief Stretch audio from left edge
     * Adjusts clip.startTime, clip.length, clip.audioStretchFactor to keep right edge fixed.
     * @param clip Clip to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalStretchFactor Original stretch factor at drag start
     */
    static inline void stretchAudioFromLeft(ClipInfo& clip, double newLength, double oldLength,
                                            double originalStretchFactor) {
        double rightEdge = clip.startTime + clip.length;

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

        double stretchRatio = newLength / oldLength;
        double newStretchFactor = originalStretchFactor * stretchRatio;
        newStretchFactor = juce::jlimit(MIN_STRETCH_FACTOR, MAX_STRETCH_FACTOR, newStretchFactor);

        newLength = oldLength * (newStretchFactor / originalStretchFactor);

        clip.length = newLength;
        clip.startTime = rightEdge - newLength;
        clip.audioStretchFactor = newStretchFactor;
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
        double originalStretchFactor = clip.audioStretchFactor;

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = clip.length - newLength;
        clip.startTime = juce::jmax(0.0, clip.startTime + lengthDelta);
        clip.length = newLength;

        // Stretch audio proportionally
        stretchAudioFromLeft(clip, newLength, oldLength, originalStretchFactor);
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
        double originalStretchFactor = clip.audioStretchFactor;

        resizeContainerFromRight(clip, newLength);

        stretchAudioFromRight(clip, newLength, oldLength, originalStretchFactor);
    }

  private:
    ClipOperations() = delete;  // Static class, no instances
};

}  // namespace magda
