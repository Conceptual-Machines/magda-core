#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <utility>

#include "ClipInfo.hpp"
#include "TempoUtils.hpp"
#include "TimeStretchModes.hpp"

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
    static constexpr double MIN_MIDI_NOTE_LENGTH_BEATS = 1.0 / 16.0;

    static inline bool hasDefaultPlacement(const ClipInfo& clip) {
        constexpr double eps = 0.000001;
        return std::abs(clip.placement.startBeat) <= eps && std::abs(clip.startBeats) <= eps &&
               std::abs(clip.placement.lengthBeats - 4.0) <= eps &&
               std::abs(clip.lengthBeats - 4.0) <= eps;
    }

    static inline void seedPlacementFromTimelineCacheIfNeeded(ClipInfo& clip, double bpm) {
        if (!isValidBpm(bpm) || !hasDefaultPlacement(clip))
            return;

        constexpr double eps = 0.000001;
        const double placementStartSeconds = clip.getTimelineStart(bpm);
        const double placementLengthSeconds = clip.getTimelineLength(bpm);
        if (std::abs(clip.startTime - placementStartSeconds) <= eps &&
            std::abs(clip.length - placementLengthSeconds) <= eps) {
            return;
        }

        clip.setPlacementBeats(clip.startTime * bpm / 60.0, clip.length * bpm / 60.0);
        clip.deriveTimesFromBeats(bpm);
    }

    struct MidiNoteRange {
        double startBeat = 0.0;
        double lengthBeats = 0.0;

        double endBeat() const {
            return startBeat + lengthBeats;
        }
    };

    // ========================================================================
    // MIDI range helpers
    // ========================================================================

    static inline MidiNoteRange getMidiVisibleRange(const ClipInfo& clip) {
        if (!clip.isMidi())
            return {};
        const double lengthBeats =
            (clip.loopEnabled && clip.loopLengthBeats > 0.0)
                ? clip.loopLengthBeats
                : (clip.placement.lengthBeats > 0.0 ? clip.placement.lengthBeats
                                                    : clip.lengthBeats);
        const double startBeat = clip.loopEnabled ? 0.0 : juce::jmax(0.0, clip.midiTrimOffset);
        return {startBeat, juce::jmax(0.0, lengthBeats)};
    }

    static inline bool clipMidiNoteToVisibleRange(const ClipInfo& clip, MidiNote& note) {
        auto range = getMidiVisibleRange(clip);
        if (range.lengthBeats <= 0.0 || note.lengthBeats <= 0.0)
            return false;

        double noteStart = note.startBeat;
        double noteEnd = note.startBeat + note.lengthBeats;
        if (noteEnd <= range.startBeat || noteStart >= range.endBeat())
            return false;

        if (noteStart < range.startBeat)
            noteStart = range.startBeat;
        if (noteEnd > range.endBeat())
            noteEnd = range.endBeat();

        if (noteEnd <= noteStart)
            return false;

        note.startBeat = noteStart;
        note.lengthBeats = noteEnd - noteStart;
        return true;
    }

    static inline bool constrainMidiNoteToVisibleRange(const ClipInfo& clip, MidiNote& note) {
        auto range = getMidiVisibleRange(clip);
        if (range.lengthBeats < MIN_MIDI_NOTE_LENGTH_BEATS)
            return false;

        note.lengthBeats = juce::jmax(MIN_MIDI_NOTE_LENGTH_BEATS, note.lengthBeats);

        const double latestStart = range.endBeat() - MIN_MIDI_NOTE_LENGTH_BEATS;
        note.startBeat = juce::jlimit(range.startBeat, latestStart, note.startBeat);

        if (note.startBeat + note.lengthBeats > range.endBeat())
            note.lengthBeats = range.endBeat() - note.startBeat;

        return note.lengthBeats > 0.0;
    }

    // ========================================================================
    // Container Operations (clip-level only)
    // ========================================================================

    static inline void setBeatPlacement(ClipInfo& clip, double startBeat, double lengthBeats,
                                        double bpm) {
        if (!isValidBpm(bpm))
            return;

        startBeat = juce::jmax(0.0, startBeat);
        lengthBeats = juce::jmax(MIN_CLIP_LENGTH * bpm / 60.0, lengthBeats);
        clip.setPlacementBeats(startBeat, lengthBeats);
        clip.deriveTimesFromBeats(bpm);
    }

    static inline void setTimelinePlacement(ClipInfo& clip, double newStartTime, double newLength,
                                            double bpm) {
        if (!isValidBpm(bpm))
            return;

        newStartTime = juce::jmax(0.0, newStartTime);
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        setBeatPlacement(clip, newStartTime * bpm / 60.0, newLength * bpm / 60.0, bpm);
    }

    static inline void setStartBeat(ClipInfo& clip, double newStartBeat, double bpm) {
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        setBeatPlacement(clip, newStartBeat, clip.placement.lengthBeats, bpm);
    }

    static inline void setTimelineStart(ClipInfo& clip, double newStartTime, double bpm) {
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        if (!isValidBpm(bpm))
            return;
        setStartBeat(clip, newStartTime * bpm / 60.0, bpm);
    }

    static inline void moveContainerBeats(ClipInfo& clip, double newStartBeat,
                                          double bpm = DEFAULT_BPM) {
        setStartBeat(clip, newStartBeat, bpm);
    }

    /**
     * @brief Move clip container to new timeline position
     * @param clip Clip to move
     * @param newStartTime New absolute timeline position (clamped to >= 0.0)
     */
    static inline void moveContainer(ClipInfo& clip, double newStartTime,
                                     double bpm = DEFAULT_BPM) {
        setTimelineStart(clip, newStartTime, bpm);
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
                                               double bpm = DEFAULT_BPM) {
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        const double clipStart = clip.getTimelineStart(bpm);
        const double clipLength = clip.getTimelineLength(bpm);
        double lengthDelta = clipLength - newLength;
        double newStartTime = juce::jmax(0.0, clipStart + lengthDelta);
        double actualDelta = newStartTime - clipStart;

        // NOTE: In auto-tempo mode, do NOT update loopLengthBeats here.
        // loopLengthBeats is the authoritative source of truth and should only
        // be updated when the user explicitly changes it, not during tempo-driven resizes.

        auto* event = clip.primaryEvent();
        if (event != nullptr && !event->sourceFilePath().isEmpty()) {
            const bool isAutoTempo = event->autoTempo && event->interpBpm > 0.0 && isValidBpm(bpm);

            // Beat mode and manual stretch differ only in how a timeline delta
            // becomes a source delta: via project BPM, or via speedRatio.
            const double sourceDelta = isAutoTempo
                                           ? (actualDelta * bpm / 60.0) * 60.0 / event->interpBpm
                                           : actualDelta * event->speedRatio;

            if (!clip.loopEnabled) {
                event->setAnchorSeconds(event->anchorSeconds() + sourceDelta);
                event->loopStartSamples = event->sourceAnchorSamples;
            } else {
                // With no region set, beat mode has no period to wrap in:
                // clipLength is timeline seconds at project tempo and
                // speedRatio is pinned to 1, so using it as a source period
                // would wrap by the wrong amount. Leave the phase alone, as
                // the beat-domain path did before the event split.
                const double sourceLength =
                    event->loopLengthSamples > 0
                        ? event->loopLengthSeconds()
                        : (isAutoTempo ? 0.0 : clipLength * event->speedRatio);
                if (sourceLength > 0.0) {
                    event->setAnchorSeconds(
                        event->loopStartSeconds() +
                        wrapPhase(event->loopPhaseSeconds() + sourceDelta, sourceLength));
                }
            }
        } else if (clip.isMidi()) {
            // MIDI phase lives in midiOffset (beats). There is no source anchor.
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

        // Clip placement is beat-domain. Seconds are derived cache values for
        // callers that still operate at the UI/bridge boundary.
        setTimelinePlacement(clip, newStartTime, newLength, bpm);
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
                                                double bpm = DEFAULT_BPM) {
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        const bool hasExplicitBeatStart = std::abs(clip.placement.startBeat) > 0.000001;
        const double currentStart = isValidBpm(bpm) && hasExplicitBeatStart
                                        ? clip.placement.startBeat * 60.0 / bpm
                                        : clip.getTimelineStart(bpm);
        setTimelinePlacement(clip, currentStart, newLength, bpm);
    }

    // ========================================================================
    // Audio Operations (clip-level fields)
    // ========================================================================

    /**
     * @brief Clamp audio source-domain fields to the known source duration.
     *
     * Offset is the playback/read phase. loopStart is the source-region anchor
     * used by loop-mode transitions and editor boundaries; non-loop sanitizing
     * must not mirror it to offset.
     */
    static inline void sanitizeAudioToSourceDuration(ClipInfo& clip, double fileDuration,
                                                     double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr || fileDuration <= 0.0)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);

        event->setLoopStartSeconds(juce::jlimit(0.0, fileDuration, event->loopStartSeconds()));

        const double availableFromLoop = fileDuration - event->loopStartSeconds();
        if (event->loopLengthSeconds() > availableFromLoop)
            event->setLoopLengthSeconds(juce::jmax(0.0, availableFromLoop));

        event->setAnchorSeconds(juce::jlimit(0.0, fileDuration, event->anchorSeconds()));

        if (!clip.loopEnabled && !event->autoTempo) {
            const double speed = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
            const double maxLength = (fileDuration - event->anchorSeconds()) / speed;
            const double currentLength = clip.getTimelineLength(bpm);
            if (currentLength > maxLength) {
                setTimelinePlacement(clip, clip.getTimelineStart(bpm),
                                     juce::jmax(ClipInfo::MIN_CLIP_LENGTH, maxLength), bpm);
            }
        }
    }

    static inline void setAudioOffsetPreservingSourceRegion(ClipInfo& clip, double newOffset,
                                                            double fileDuration = 0.0,
                                                            double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);

        if (fileDuration > 0.0)
            newOffset = juce::jmin(newOffset, fileDuration);
        event->setAnchorSeconds(juce::jmax(0.0, newOffset));

        if (!clip.loopEnabled && !event->autoTempo && fileDuration > 0.0) {
            const double speed = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
            const double currentLength = clip.getTimelineLength(bpm);
            const double maxLength = (fileDuration - event->anchorSeconds()) / speed;
            if (currentLength > maxLength) {
                setTimelinePlacement(clip, clip.getTimelineStart(bpm),
                                     juce::jmax(MIN_CLIP_LENGTH, maxLength), bpm);
            }
        }
    }

    static inline void setAudioLoopPhaseClamped(ClipInfo& clip, double phase) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;

        event->setAnchorSeconds(juce::jmax(0.0, event->loopStartSeconds() + phase));
    }

    /**
     * @brief Trim audio from left edge
     * Adjusts source offset and timeline beat placement.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromLeft(ClipInfo& clip, double trimAmount,
                                         double fileDuration = 0.0, double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        const double oldOffset = event->anchorSeconds();
        double newOffset = oldOffset + trimAmount * event->speedRatio;

        if (fileDuration > 0.0) {
            newOffset = juce::jmin(newOffset, fileDuration);
        }
        newOffset = juce::jmax(0.0, newOffset);

        double timelineDelta = (newOffset - oldOffset) / event->speedRatio;

        event->setAnchorSeconds(newOffset);
        event->loopStartSamples = event->sourceAnchorSamples;
        const double newStartTime = juce::jmax(0.0, clip.getTimelineStart(bpm) + timelineDelta);
        const double newLength =
            juce::jmax(MIN_CLIP_LENGTH, clip.getTimelineLength(bpm) - timelineDelta);
        setTimelinePlacement(clip, newStartTime, newLength, bpm);
    }

    /**
     * @brief Trim audio from right edge
     * Adjusts timeline beat placement and loopLength.
     * @param clip Clip to modify
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no file constraint)
     */
    static inline void trimAudioFromRight(ClipInfo& clip, double trimAmount,
                                          double fileDuration = 0.0, double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        double newLength = clip.getTimelineLength(bpm) - trimAmount;

        if (fileDuration > 0.0) {
            double maxLength = (fileDuration - event->anchorSeconds()) / event->speedRatio;
            newLength = juce::jmin(newLength, maxLength);
        }

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        const double currentStart = clip.getTimelineStart(bpm);
        setTimelinePlacement(clip, currentStart, newLength, bpm);
    }

    /**
     * @brief Stretch audio from right edge
     * Adjusts timeline beat placement and speedRatio.
     * @param clip Clip to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    static inline void stretchAudioFromRight(ClipInfo& clip, double newLength, double oldLength,
                                             double originalSpeedRatio, double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

        double stretchRatio = newLength / oldLength;
        double newSpeedRatio = originalSpeedRatio / stretchRatio;
        newSpeedRatio = juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, newSpeedRatio);

        newLength = oldLength * (originalSpeedRatio / newSpeedRatio);

        const double currentStart = clip.getTimelineStart(bpm);
        setTimelinePlacement(clip, currentStart, newLength, bpm);
        event->speedRatio = newSpeedRatio;

        // Keep the source region in sync for non-looped events
        if (!clip.loopEnabled)
            event->setLoopLengthSeconds(event->timelineToSource(clip.getTimelineLength(bpm)));
    }

    /**
     * @brief Stretch audio from left edge
     * Adjusts timeline beat placement and speedRatio to keep the right edge fixed.
     * @param clip Clip to stretch
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    static inline void stretchAudioFromLeft(ClipInfo& clip, double newLength, double oldLength,
                                            double originalSpeedRatio, double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        double rightEdge = clip.getTimelineEnd(bpm);

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);

        double stretchRatio = newLength / oldLength;
        double newSpeedRatio = originalSpeedRatio / stretchRatio;
        newSpeedRatio = juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, newSpeedRatio);

        newLength = oldLength * (originalSpeedRatio / newSpeedRatio);
        if (rightEdge > 0.0 && newLength > rightEdge) {
            newLength = juce::jmax(MIN_CLIP_LENGTH, rightEdge);
            stretchRatio = newLength / oldLength;
            newSpeedRatio =
                juce::jlimit(MIN_SPEED_RATIO, MAX_SPEED_RATIO, originalSpeedRatio / stretchRatio);
            newLength = oldLength * (originalSpeedRatio / newSpeedRatio);
        }

        const double newStart = rightEdge - newLength;
        setTimelinePlacement(clip, newStart, newLength, bpm);
        event->speedRatio = newSpeedRatio;

        // Keep the source region in sync for non-looped events
        if (!clip.loopEnabled)
            event->setLoopLengthSeconds(event->timelineToSource(clip.getTimelineLength(bpm)));
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
        auto* event = clip.primaryEvent();
        if (event == nullptr || event->sourceFilePath().isEmpty()) {
            resizeContainerFromLeft(clip, newLength);
            return;
        }

        double oldLength = clip.getTimelineLength(DEFAULT_BPM);
        double originalSpeedRatio = event->speedRatio;

        newLength = juce::jmax(MIN_CLIP_LENGTH, newLength);
        double lengthDelta = oldLength - newLength;
        setTimelinePlacement(clip, clip.getTimelineStart(DEFAULT_BPM) + lengthDelta, newLength,
                             DEFAULT_BPM);

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
        auto* event = clip.primaryEvent();
        if (event == nullptr || event->sourceFilePath().isEmpty()) {
            resizeContainerFromRight(clip, newLength);
            return;
        }

        double oldLength = clip.getTimelineLength(DEFAULT_BPM);
        double originalSpeedRatio = event->speedRatio;

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
                                               double newLength, double bpm = DEFAULT_BPM) {
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        setTimelinePlacement(clip, newStartTime, newLength, bpm);
    }

    /**
     * @brief Update only the timeline placement length for an auto-tempo clip.
     */
    static inline void setAutoTempoPlacementLengthBeats(ClipInfo& clip, double newTotalBeats,
                                                        double bpm) {
        if (newTotalBeats <= 0.0)
            return;

        if (!isValidBpm(bpm))
            return;

        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        double startBeat = clip.getStartBeats(bpm);

        clip.setPlacementBeats(startBeat, newTotalBeats);
        clip.deriveTimesFromBeats(bpm);
    }

    /**
     * @brief Stretch to absolute target speed/length (for drag preview).
     * For autoTempo clips, changes lengthBeats instead of speedRatio.
     * @param clip Clip to stretch
     * @param newSpeedRatio New speed ratio (ignored for autoTempo)
     * @param newLength New clip length in seconds
     * @param bpm Current project tempo
     */
    /**
     * @brief Re-interpret an autoTempo (beat-mode) clip's source as ratio× the
     * beats so the engine time-stretches the audio to the new length.
     *
     * autoTempo clips carry speedRatio == 1 and stretch via the source
     * interpretation (bpm / totalBeats) instead, so a speedRatio change is a
     * no-op for them. The source file's seconds are fixed, so only the
     * source-BEAT fields scale (drag right = ratio > 1 = more beats = slower
     * playback); the seconds-derived loop/offset values stay put because bpm
     * scales with them.
     */
    static inline void applyAutoTempoStretch(ClipInfo& clip, double ratio) {
        auto* event = clip.primaryEvent();
        if (event == nullptr || !(ratio > 0.0) || ratio == 1.0)
            return;
        if (event->interpTotalBeats > 0.0)
            event->interpTotalBeats *= ratio;
        if (event->interpBpm > 0.0)
            event->interpBpm *= ratio;
        // The anchor and loop region are fixed source samples; their beat views
        // scale with interpBpm on their own, so there is nothing else to touch.
    }

    static inline void stretchAbsolute(ClipInfo& clip, double newSpeedRatio, double newLength,
                                       double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        const double currentStart = clip.getTimelineStart(bpm);
        const double oldLengthBeats = clip.placement.lengthBeats;
        setTimelinePlacement(clip, currentStart, newLength, bpm);
        if (event != nullptr && event->autoTempo && isValidBpm(bpm)) {
            double newBeats = newLength * bpm / 60.0;
            if (oldLengthBeats > 0.0)
                applyAutoTempoStretch(clip, newBeats / oldLengthBeats);
            setAutoTempoPlacementLengthBeats(clip, newBeats, bpm);
        } else if (event != nullptr) {
            event->speedRatio = newSpeedRatio;
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
                                               double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        const double oldLengthBeats = clip.placement.lengthBeats;
        setTimelinePlacement(clip, rightEdge - newLength, newLength, bpm);
        if (event != nullptr && event->autoTempo && isValidBpm(bpm)) {
            double newBeats = newLength * bpm / 60.0;
            if (oldLengthBeats > 0.0)
                applyAutoTempoStretch(clip, newBeats / oldLengthBeats);
            setAutoTempoPlacementLengthBeats(clip, newBeats, bpm);
        } else if (event != nullptr) {
            event->speedRatio = newSpeedRatio;
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
     * TE's loopStartBeats/loopLengthBeats are in source-file beats (clamped to
     * loopInfo.getNumBeats()), which is exactly the beat view of the event's
     * source-domain loop region.
     *
     * @param event The event to calculate for
     * @return Pair of (loopStartBeats, loopLengthBeats) in SOURCE beats
     */
    static inline std::pair<double, double> getAutoTempoBeatRange(const AudioEvent& event) {
        if (!event.autoTempo && !event.warpEnabled)
            return {0.0, 0.0};

        double start = event.loopStartBeats();
        double length = event.loopLengthBeats();
        if (length <= 0.0)
            return {0.0, 0.0};

        // TE's setLoopRangeBeats clamps the end to loopInfo.getNumBeats(). In
        // time-based mode loops can wrap past the file end, beat-based mode
        // cannot, so shift the start back until the whole region fits.
        if (event.interpTotalBeats > 0.0) {
            if (length > event.interpTotalBeats) {
                length = event.interpTotalBeats;
                start = 0.0;
            } else if (start + length > event.interpTotalBeats) {
                start = juce::jmax(0.0, event.interpTotalBeats - length);
            }
        }
        return {start, length};
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
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;

        event->autoTempo = true;
        clip.setPlacementBeats(clip.placement.startBeat, lengthBeats);

        // Source beats need the SOURCE tempo to become source seconds. Fall
        // back to the project tempo while detection has not produced one yet.
        const double srcBpm = event->interpBpm > 0.0 ? event->interpBpm : bpm;
        if (srcBpm > 0.0) {
            const double regionBeats = loopLengthBeats > 0.0 ? loopLengthBeats : lengthBeats;
            event->setLoopLengthSeconds(regionBeats * 60.0 / srcBpm);
            event->setLoopStartSeconds(loopStartBeats * 60.0 / srcBpm);
        }

        // Update time-based fields (derived values)
        clip.setLengthFromBeats(lengthBeats, bpm);

        // Auto-tempo requires speedRatio=1.0
        event->speedRatio = 1.0;
    }

    /**
     * @brief Toggle auto-tempo mode (converts between time↔beat storage)
     * @param clip Clip to modify
     * @param enabled Enable auto-tempo mode
     * @param bpm Current tempo for conversion
     */
    static inline void setAutoTempo(ClipInfo& clip, bool enabled, double bpm) {
        auto* event = clip.primaryEvent();
        if (event == nullptr || event->autoTempo == enabled)
            return;

        if (enabled && !isValidBpm(bpm))
            return;

        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);

        event->autoTempo = enabled;

        if (enabled) {
            event->analogPitch = false;  // Analog pitch is incompatible with autoTempo

            // Auto-tempo requires time-stretching. Preserve any explicitly selected
            // engine, otherwise enable the default quality engine.
            if (event->timeStretchMode == time_stretch_mode::kDisabled)
                event->timeStretchMode = time_stretch_mode::kSignalsmith;

            // Preserve current timeline position in beat-domain placement.
            clip.setPlacementBeats(clip.getStartBeats(bpm), clip.placement.lengthBeats);

            // Enable looping (required for TE's autoTempo beat range to work)
            if (!clip.loopEnabled) {
                clip.loopEnabled = true;
                event->loopStartSamples = event->sourceAnchorSamples;
                event->setLoopLengthSeconds(event->timelineToSource(clip.getTimelineLength(bpm)));
            }

            // Issue #1157: when a full, untrimmed source file carries source
            // interpretation beats, default placement length to that musical
            // extent so a freshly-dropped loop becomes exactly its natural
            // length on toggling BEAT. If the user has already trimmed the
            // clip, preserve the edited timeline span instead of expanding
            // back to the full source loop.
            //
            // Prefer interpretation-derived duration (totalBeats × 60 /
            // interpBpm) — the interpretation is the calibrated musical view of
            // the file, while the pooled source duration is the raw file fact
            // and may predate a later detection pass.
            double naturalSourceDuration = 0.0;
            if (event->interpBpm > 0.0 && event->interpTotalBeats > 0.0) {
                naturalSourceDuration = event->interpTotalBeats * 60.0 / event->interpBpm;
            } else if (event->sourceDurationSeconds() > 0.0) {
                naturalSourceDuration = event->sourceDurationSeconds();
            }
            const auto sourceSpan = event->timelineToSource(clip.getTimelineLength(bpm));
            const bool coversFullSource = naturalSourceDuration > 0.0 &&
                                          event->anchorSeconds() <= 0.001 &&
                                          std::abs(sourceSpan - naturalSourceDuration) <= 0.001;

            if (coversFullSource && event->interpTotalBeats > 0.0)
                clip.setPlacementBeats(clip.placement.startBeat, event->interpTotalBeats);
            else
                clip.setPlacementBeats(clip.placement.startBeat, clip.getLengthInBeats(bpm));

            // A loop region already set is source-domain and needs no
            // re-derivation. Only a clip that had none needs one seeded, in
            // source seconds via the source tempo (project tempo while
            // detection has not produced one yet).
            if (event->loopLengthSamples <= 0) {
                const double srcBpm = event->interpBpm > 0.0 ? event->interpBpm : bpm;
                const double regionBeats = event->interpTotalBeats > 0.0
                                               ? event->interpTotalBeats
                                               : clip.placement.lengthBeats;
                if (srcBpm > 0.0)
                    event->setLoopLengthSeconds(regionBeats * 60.0 / srcBpm);
                event->loopStartSamples = 0;
            }

            // Force speedRatio to 1.0 (TE requirement for autoTempo)
            event->speedRatio = 1.0;
        } else if (clip.loopEnabled && event->loopLengthSamples > 0) {
            // Timeline placement remains beat-domain. The source region is
            // already in samples and survives the mode change untouched; only
            // the read phase needs re-wrapping into the region.
            event->setAnchorSeconds(
                event->loopStartSeconds() +
                wrapPhase(event->loopPhaseSeconds(), event->loopLengthSeconds()));
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

        clip.setPlacementBeats(clip.placement.startBeat, newLengthBeats);
        clip.deriveTimesFromBeats(bpm);
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

        const double oldEndBeat = clip.placement.endBeat();
        clip.setPlacementBeats(clip.placement.startBeat, newLengthBeats);
        clip.deriveTimesFromBeats(bpm);

        // Adjust placement start to keep right edge fixed.
        double newStartBeat = juce::jmax(0.0, oldEndBeat - newLengthBeats);
        clip.setPlacementBeats(newStartBeat, newLengthBeats);
        clip.deriveTimesFromBeats(bpm);
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
    static inline void moveLoopStart(ClipInfo& clip, double newLoopStart, double fileDuration,
                                     double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        seedPlacementFromTimelineCacheIfNeeded(clip, bpm);
        event->setLoopStartSeconds(newLoopStart);
        // Clamp the loop region to the audio available from the new start
        if (fileDuration > 0.0) {
            const double avail = fileDuration - event->loopStartSeconds();
            if (event->loopLengthSeconds() > avail)
                event->setLoopLengthSeconds(juce::jmax(0.0, avail));
        }
        if (!clip.loopEnabled && !event->autoTempo && fileDuration > 0.0) {
            const double speed = event->speedRatio > 0.0 ? event->speedRatio : 1.0;
            const double maxLength = (fileDuration - event->anchorSeconds()) / speed;
            if (clip.getTimelineLength(bpm) > maxLength) {
                setTimelinePlacement(clip, clip.getTimelineStart(bpm),
                                     juce::jmax(MIN_CLIP_LENGTH, maxLength), bpm);
            }
        }
    }

    /**
     * @brief Set source extent via timeline extent (editor right-edge drag)
     * Updates loopLength from timeline extent.
     * For non-looped clips, also updates timeline beat placement.
     * @param clip Clip to modify
     * @param newTimelineExtent New extent in timeline seconds
     */
    static inline void resizeSourceExtent(ClipInfo& clip, double newTimelineExtent,
                                          double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        event->setLoopLengthSeconds(event->timelineToSource(newTimelineExtent));
        if (!clip.loopEnabled) {
            const double currentStart = clip.getTimelineStart(bpm);
            setTimelinePlacement(clip, currentStart, newTimelineExtent, bpm);
        }
    }

    /**
     * @brief Stretch in editor (changes speedRatio, scales timeline beat placement,
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
                                     double dragStartExtent, double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        event->speedRatio = newSpeedRatio;
        const double currentStart = clip.getTimelineStart(bpm);
        setTimelinePlacement(clip, currentStart, dragStartClipLength * clipLengthScaleFactor, bpm);
        // In loop mode, adjust the source region to keep the loop markers fixed
        // on the timeline
        if (clip.loopEnabled && event->loopLengthSamples > 0)
            event->setLoopLengthSeconds(dragStartExtent / newSpeedRatio);
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
                                             double rightEdge, double bpm = DEFAULT_BPM) {
        auto* event = clip.primaryEvent();
        if (event == nullptr)
            return;
        event->speedRatio = newSpeedRatio;
        const double newLength = dragStartClipLength * clipLengthScaleFactor;
        setTimelinePlacement(clip, rightEdge - newLength, newLength, bpm);
        // In loop mode, adjust the source region to keep the loop markers fixed
        // on the timeline
        if (clip.loopEnabled && event->loopLengthSamples > 0)
            event->setLoopLengthSeconds(dragStartExtent / newSpeedRatio);
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
        if (!clip.isMidi())
            return;
        std::vector<MidiNote> flatNotes;
        double clipLen = clip.lengthBeats;

        if (clip.loopEnabled && clip.loopLengthBeats > 0.0) {
            double loopLen = clip.loopLengthBeats;
            double phase = wrapPhase(clip.midiOffset, loopLen);

            // Include the final partial cycle introduced by the phase offset.
            // A clip starting one beat into a two-beat loop needs the cycle
            // beginning at clipLen - 1 to render its final beat.
            int numCycles = static_cast<int>(std::ceil((clipLen + phase) / loopLen));

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
            clip.loopLengthBeats = 0.0;
            clip.loopStartBeats = 0.0;
            clip.midiOffset = 0.0;
            clip.midiTrimOffset = 0.0;
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

        // The notes now ARE the unrolled loop, so the clip has to stop looping.
        // Leaving the flag on made TE loop the first cycle over the whole clip
        // and made getMidiVisibleRange clip the note list to one loop length,
        // so a flattened clip played only its first loop no matter how many
        // cycles had just been written into it.
        clip.loopEnabled = false;
        clip.midiOffset = 0.0;
        clip.midiTrimOffset = 0.0;

        clip.midiNotes = std::move(flatNotes);
    }

  private:
    ClipOperations() = delete;  // Static class, no instances
};

}  // namespace magda
