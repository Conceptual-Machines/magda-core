#pragma once

#include <juce_core/juce_core.h>

#include "ClipInfo.hpp"

namespace magda {

/**
 * @brief Centralized utility class for all clip and audio source operations
 *
 * Provides static methods for:
 * - Container operations (clip boundaries only)
 * - Content operations (audio source properties only)
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
    static constexpr double MIN_SOURCE_LENGTH = 0.1;
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
     * @brief Resize clip container from left edge (absolute mode)
     * Moves clip start time, adjusts length. Audio stays at same timeline position.
     * @param clip Clip to resize
     * @param newLength New clip length (clamped to >= MIN_CLIP_LENGTH)
     */
    static inline void resizeContainerFromLeft(ClipInfo& clip, double newLength) {
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double oldStartTime = clip.startTime;
        double lengthDelta = clip.length - newLength;
        clip.startTime = juce::jmax(0.0, clip.startTime + lengthDelta);
        double actualStartDelta = clip.startTime - oldStartTime;
        clip.length = newLength;

        // Compensate audio source positions so they stay at the same absolute
        // timeline position. source.position is relative to clip.startTime,
        // so when clip start moves, we adjust by the opposite amount.
        // Then trim any audio that now falls before clip start.
        for (auto& source : clip.audioSources) {
            source.position -= actualStartDelta;

            // Trim audio that extends before clip start (negative position)
            if (source.position < 0.0) {
                double trimAmount = -source.position;                // timeline seconds to trim
                source.offset += trimAmount / source.stretchFactor;  // advance file offset
                source.length -= trimAmount;                         // shorten visible duration
                source.length = juce::jmax(MIN_SOURCE_LENGTH, source.length);
                source.position = 0.0;
            }
        }
    }

    /**
     * @brief Resize clip container from right edge
     * Only changes length, start time unchanged.
     * @param clip Clip to resize
     * @param newLength New clip length (clamped to >= MIN_CLIP_LENGTH)
     */
    static inline void resizeContainerFromRight(ClipInfo& clip, double newLength) {
        clip.length = juce::jmax(MIN_CLIP_LENGTH, newLength);
        // startTime unchanged
    }

    // ========================================================================
    // Audio Source Operations (content-level only)
    // ========================================================================

    /**
     * @brief Trim/extend audio source from left edge
     * Adjusts file offset, position, and length.
     * Positive trimAmount trims (removes from start), negative extends (reveals more from start).
     * @param source Audio source to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimSourceFromLeft(AudioSource& source, double trimAmount,
                                          double fileDuration = 0.0) {
        // Calculate file offset delta (accounting for stretch)
        double fileDelta = trimAmount / source.stretchFactor;
        double newOffset = source.offset + fileDelta;

        // Constrain to file bounds
        if (fileDuration > 0.0) {
            newOffset = juce::jmin(newOffset, fileDuration);
        }
        newOffset = juce::jmax(0.0, newOffset);

        // Calculate actual timeline delta achieved
        double actualFileDelta = newOffset - source.offset;
        double timelineDelta = actualFileDelta * source.stretchFactor;

        // Update properties
        source.offset = newOffset;
        source.position = juce::jmax(0.0, source.position + timelineDelta);
        source.length = juce::jmax(MIN_SOURCE_LENGTH, source.length - timelineDelta);
    }

    /**
     * @brief Trim/extend audio source from right edge
     * Adjusts length only, offset and position unchanged.
     * Positive trimAmount trims (reduces length), negative extends (increases length).
     * @param source Audio source to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimSourceFromRight(AudioSource& source, double trimAmount,
                                           double fileDuration = 0.0) {
        double newLength = source.length - trimAmount;

        // Constrain to file bounds
        if (fileDuration > 0.0) {
            double maxLength = (fileDuration - source.offset) * source.stretchFactor;
            newLength = juce::jmin(newLength, maxLength);
        }

        source.length = juce::jmax(MIN_SOURCE_LENGTH, newLength);
    }

    /**
     * @brief Stretch audio source from right edge
     * Adjusts length and stretchFactor, offset unchanged.
     * @param source Audio source to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalStretchFactor Original stretch factor at drag start
     * @param clipLength Clip container length (for boundary constraint)
     */
    static inline void stretchSourceFromRight(AudioSource& source, double newLength,
                                              double oldLength, double originalStretchFactor,
                                              double clipLength) {
        newLength = juce::jmax(MIN_SOURCE_LENGTH, newLength);

        // Constrain to clip container
        double maxLength = clipLength - source.position;
        newLength = juce::jmin(newLength, maxLength);

        // Calculate stretch ratio from original and new factor
        double stretchRatio = newLength / oldLength;
        double newStretchFactor = originalStretchFactor * stretchRatio;
        newStretchFactor = juce::jlimit(MIN_STRETCH_FACTOR, MAX_STRETCH_FACTOR, newStretchFactor);

        // Back-compute length after clamping
        newLength = oldLength * (newStretchFactor / originalStretchFactor);
        newLength = juce::jmin(newLength, maxLength);

        source.length = newLength;
        source.stretchFactor = newStretchFactor;
    }

    /**
     * @brief Stretch audio source from left edge
     * Adjusts position, length, and stretchFactor to keep right edge fixed.
     * @param source Audio source to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalPosition Original position at drag start
     * @param originalStretchFactor Original stretch factor at drag start
     */
    static inline void stretchSourceFromLeft(AudioSource& source, double newLength,
                                             double oldLength, double originalPosition,
                                             double originalStretchFactor) {
        // Right edge stays fixed (calculated from ORIGINAL position, not current)
        double rightEdge = originalPosition + oldLength;

        newLength = juce::jmax(MIN_SOURCE_LENGTH, newLength);
        newLength = juce::jmin(newLength, rightEdge);  // Can't exceed right edge

        // Calculate stretch ratio from original and new factor
        double stretchRatio = newLength / oldLength;
        double newStretchFactor = originalStretchFactor * stretchRatio;
        newStretchFactor = juce::jlimit(MIN_STRETCH_FACTOR, MAX_STRETCH_FACTOR, newStretchFactor);

        // Back-compute length after clamping
        newLength = oldLength * (newStretchFactor / originalStretchFactor);
        newLength = juce::jmin(newLength, rightEdge);

        source.length = newLength;
        source.position = rightEdge - newLength;
        source.stretchFactor = newStretchFactor;
    }

    /**
     * @brief Move audio source within clip container
     * Changes position only, all other properties unchanged.
     * @param source Audio source to move
     * @param newPosition New position within clip (clamped to >= 0.0)
     * @param clipLength Clip container length (for boundary constraint)
     */
    static inline void moveSource(AudioSource& source, double newPosition, double clipLength) {
        juce::ignoreUnused(clipLength);
        source.position = juce::jmax(0.0, newPosition);
        // Could add constraint: position + length <= clipLength
    }

    // ========================================================================
    // Compound Operations (container + content)
    // ========================================================================

    /**
     * @brief Stretch clip from left edge (arrangement-level operation)
     * Resizes container from left AND stretches audio source proportionally.
     * Used by ClipComponent StretchLeft.
     * @param clip Clip to stretch
     * @param newLength New clip length
     */
    static inline void stretchClipFromLeft(ClipInfo& clip, double newLength) {
        if (clip.type != ClipType::Audio || clip.audioSources.empty()) {
            // For non-audio clips, just resize container
            resizeContainerFromLeft(clip, newLength);
            return;
        }

        double oldLength = clip.length;
        double originalPosition = clip.audioSources[0].position;
        double originalStretchFactor = clip.audioSources[0].stretchFactor;

        // Resize container only (no source trimming — stretch handles source)
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = clip.length - newLength;
        clip.startTime = juce::jmax(0.0, clip.startTime + lengthDelta);
        clip.length = newLength;

        // Stretch audio source proportionally (handles position, length, stretchFactor)
        stretchSourceFromLeft(clip.audioSources[0], newLength, oldLength, originalPosition,
                              originalStretchFactor);
    }

    /**
     * @brief Stretch clip from right edge (arrangement-level operation)
     * Resizes container from right AND stretches audio source proportionally.
     * Used by ClipComponent StretchRight.
     * @param clip Clip to stretch
     * @param newLength New clip length
     */
    static inline void stretchClipFromRight(ClipInfo& clip, double newLength) {
        if (clip.type != ClipType::Audio || clip.audioSources.empty()) {
            resizeContainerFromRight(clip, newLength);
            return;
        }

        double oldLength = clip.length;
        double originalStretchFactor = clip.audioSources[0].stretchFactor;

        // Resize container from right
        resizeContainerFromRight(clip, newLength);

        // Stretch audio source proportionally
        stretchSourceFromRight(clip.audioSources[0], newLength, oldLength, originalStretchFactor,
                               newLength);
    }

  private:
    ClipOperations() = delete;  // Static class, no instances
};

}  // namespace magda
