#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

#include "ClipTypes.hpp"
#include "TrackTypes.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief MIDI note data for MIDI clips
 */
struct MidiNote {
    int noteNumber = 60;       // MIDI note number (0-127)
    int velocity = 100;        // Note velocity (0-127)
    double startBeat = 0.0;    // Start position in beats within clip
    double lengthBeats = 1.0;  // Duration in beats
};

/**
 * @brief Clip data structure containing all clip properties
 */
struct ClipInfo {
    ClipId id = INVALID_CLIP_ID;
    TrackId trackId = INVALID_TRACK_ID;
    juce::String name;
    juce::Colour colour;
    ClipType type = ClipType::MIDI;
    ClipView view = ClipView::Arrangement;  // Which view this clip belongs to

    // Timeline position
    double startTime = 0.0;  // Position on timeline (seconds) - only for Arrangement view
    double length = 4.0;     // Duration (seconds)

    // Beat-based position (only used when autoTempo = true in Arrangement view)
    double startBeats = -1.0;  // Start position in beats (-1 = not set/use startTime)

    // Audio-specific properties (flat model: one clip = one file reference)
    juce::String audioFilePath;   // Path to audio file
    double sourceNumBeats = 0.0;  // Beat count from source file metadata (TE loopInfo)
    double sourceBPM = 0.0;       // Source file BPM (from TE loopInfo, 0 = unknown)

    /// Populate source metadata from engine (only sets if not already populated)
    void setSourceMetadata(double numBeats, double bpm) {
        if (numBeats > 0.0 && sourceNumBeats <= 0.0)
            sourceNumBeats = numBeats;
        if (bpm > 0.0 && sourceBPM <= 0.0)
            sourceBPM = bpm;
    }

    // =========================================================================
    // Audio playback parameters (TE-aligned terminology)
    // =========================================================================

    // Source offset - where to start reading from source file
    // TE: Clip::offset (but TE stores in stretched time, we use source time)
    double offset = 0.0;  // Start position in source file (source-time seconds)

    // Looping - defines the region that loops
    // TE: AudioClipBase::loopStart, loopLength, isLooping()
    bool loopEnabled = false;  // Whether to loop the source region
    double loopStart = 0.0;    // Where loop region starts in source file (source-time seconds)
    double loopLength = 0.0;   // Length of loop region (source-time seconds, 0 = use clip length)

    // Time stretch
    // TE: Clip::speedRatio
    // speedRatio is a SPEED FACTOR (NOT stretch factor!)
    // Formula: timeline_seconds = source_seconds / speedRatio
    // speedRatio = 1.0: normal playback
    // speedRatio = 2.0: 2x faster (half timeline duration)
    // speedRatio = 0.5: 2x slower (double timeline duration)
    double speedRatio = 1.0;  // Playback speed ratio (1.0 = original, 2.0 = 2x speed/half duration)

    bool warpEnabled = false;  // Whether warp markers are active on this clip
    int timeStretchMode = 0;   // TimeStretcher::Mode (0 = default/auto)

    // =========================================================================
    // Auto-tempo / Musical mode (beat-based length)
    // =========================================================================
    // When autoTempo=true:
    // - Beat values are authoritative, time values are derived from BPM
    // - TE's autoTempo is enabled, clips maintain fixed musical length
    // - speedRatio must be 1.0 (TE requirement)
    // When autoTempo=false (default):
    // - Time values are authoritative (current behavior)
    // - Clips maintain fixed absolute time length regardless of BPM
    bool autoTempo = false;  // Enable beat-based length (musical mode)

    // Beat-based loop properties (only used when autoTempo = true)
    // TE: AudioClipBase::loopStartBeats, loopLengthBeats
    double loopStartBeats = 0.0;   // Loop start in beats (relative to file start)
    double loopLengthBeats = 0.0;  // Loop length in beats (0 = derive from clip length)

    // Pitch
    bool autoPitch = false;
    int autoPitchMode = 0;     // 0=pitchTrack, 1=chordTrackMono, 2=chordTrackPoly
    float pitchChange = 0.0f;  // -48 to +48 semitones
    int transpose = 0;         // -24 to +24 semitones (only when !autoPitch)

    // Beat Detection
    bool autoDetectBeats = false;
    float beatSensitivity = 0.5f;

    // Playback
    bool isReversed = false;

    // Per-Clip Mix
    float gainDB = 0.0f;
    float pan = 0.0f;  // -1.0 to 1.0

    // Fades
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    int fadeInType = 1;  // AudioFadeCurve::Type
    int fadeOutType = 1;
    int fadeInBehaviour = 0;  // 0=gainFade, 1=speedRamp
    int fadeOutBehaviour = 0;
    bool autoCrossfade = false;

    // Channels
    bool leftChannelActive = true;
    bool rightChannelActive = true;

    // MIDI-specific properties
    std::vector<MidiNote> midiNotes;
    double midiOffset = 0.0;  // Start offset in beats (for non-destructive trim)

    // Session view properties
    int sceneIndex = -1;     // -1 = not in session view (arrangement only)
    bool isPlaying = false;  // Currently playing in session
    bool isQueued = false;   // Queued to start

    // Session launch properties
    LaunchMode launchMode = LaunchMode::Trigger;
    LaunchQuantize launchQuantize = LaunchQuantize::None;

    // Constants
    static constexpr double MIN_CLIP_LENGTH = 0.1;

    // Helpers
    double getEndTime() const {
        return startTime + length;
    }

    /// Convert source-time to timeline-time (speed-factor semantics: timeline = source /
    /// speedRatio)
    double sourceToTimeline(double sourceTime) const {
        return sourceTime / speedRatio;  // Faster = shorter timeline
    }

    /// Convert timeline-time to source-time (speed-factor semantics: source = timeline *
    /// speedRatio)
    double timelineToSource(double timelineTime) const {
        return timelineTime * speedRatio;  // Timeline × speed = source distance
    }

    /// Effective source length: loopLength if set, otherwise derived from clip length
    double getSourceLength() const {
        return loopLength > 0.0 ? loopLength : timelineToSource(length);
    }

    /// Source length expressed in timeline seconds
    double getSourceLengthOnTimeline() const {
        return sourceToTimeline(getSourceLength());
    }

    /// Loop phase: offset relative to loopStart (meaningful in loop mode)
    double getLoopPhase() const {
        return offset - loopStart;
    }

    /// TE offset: phase within the loop region, in stretched time
    double getTeOffset() const {
        return (offset - loopStart) * speedRatio;
    }

    /// TE loop start in stretched time
    double getTeLoopStart() const {
        return loopStart * speedRatio;
    }

    /// TE loop end in stretched time
    double getTeLoopEnd() const {
        return (loopStart + getSourceLength()) * speedRatio;
    }

    /// Sync loopStart to match offset (keeps loop region anchored to playback start)
    void syncLoopStartToOffset() {
        loopStart = offset;
    }

    /// Set loopLength from a timeline-time extent (converts to source-time)
    void setLoopLengthFromTimeline(double timelineLength) {
        loopLength = timelineToSource(timelineLength);
    }

    /// Clamp clip length so a non-looped clip doesn't exceed the available source audio.
    /// @param fileDuration Total duration of the audio file (seconds)
    void clampLengthToSource(double fileDuration) {
        if (!loopEnabled && fileDuration > 0.0) {
            double available = fileDuration - offset;
            double maxLength = available / speedRatio;
            if (length > maxLength) {
                length = juce::jmax(MIN_CLIP_LENGTH, maxLength);
            }
        }
    }

    bool containsTime(double time) const {
        return time >= startTime && time < getEndTime();
    }

    bool overlaps(double start, double end) const {
        return startTime < end && getEndTime() > start;
    }

    bool overlaps(const ClipInfo& other) const {
        return overlaps(other.startTime, other.getEndTime());
    }

    // =========================================================================
    // Auto-tempo helpers
    // =========================================================================

    /// Get effective loop length for display/operations
    /// Returns beat length when autoTempo=true, time length otherwise
    double getEffectiveLoopLength() const {
        if (autoTempo) {
            return loopLengthBeats;
        }
        return loopLength;
    }

    /// Convert clip length to beats (using current tempo)
    double getLengthInBeats(double bpm) const {
        // beats = (seconds * bpm) / 60
        return (length * bpm) / 60.0;
    }

    /// Set clip length from beats (updates `length` field based on BPM)
    void setLengthFromBeats(double beats, double bpm) {
        // seconds = (beats * 60) / bpm
        length = (beats * 60.0) / bpm;
    }

    /// Get clip start position in beats (single source of truth for display)
    /// Returns stored beat value in autoTempo mode, calculates from time otherwise
    double getStartBeats(double bpm) const {
        if (autoTempo) {
            return startBeats;  // Authoritative beat value
        }
        // Calculate from time
        return (startTime * bpm) / 60.0;
    }

    /// Get clip end position in beats (single source of truth for display)
    /// Returns start + length in beats, using authoritative values based on mode
    double getEndBeats(double bpm) const {
        if (autoTempo) {
            return startBeats + loopLengthBeats;  // Both authoritative
        }
        // Calculate from time
        double startBeats = (startTime * bpm) / 60.0;
        double lengthBeats = (length * bpm) / 60.0;
        return startBeats + lengthBeats;
    }

    // Default clip colors (different palette from tracks)
    static inline const std::array<juce::uint32, 8> defaultColors = {
        0xFF6688CC,  // Light Blue
        0xFF66CCAA,  // Teal
        0xFFAACC66,  // Lime
        0xFFCCCC66,  // Yellow
        0xFFCCAA66,  // Orange
        0xFFCC6666,  // Red
        0xFFCC66CC,  // Pink
        0xFF8866CC,  // Purple
    };

    static juce::Colour getDefaultColor(int index) {
        return juce::Colour(defaultColors[index % defaultColors.size()]);
    }
};

}  // namespace magda
