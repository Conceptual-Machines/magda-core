#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

#include "ClipTypes.hpp"
#include "TrackTypes.hpp"
#include "TypeIds.hpp"

namespace magda {

/// Wrap a value into [0, period). Used for loop phase calculations.
inline double wrapPhase(double value, double period) {
    if (period <= 0.0)
        return 0.0;
    double result = std::fmod(value, period);
    if (result < 0.0)
        result += period;
    return result;
}

/** Fade curve type — matches tracktion::AudioFadeCurve::Type values */
enum class FadeCurve : int { Linear = 1, Convex = 2, Concave = 3, SCurve = 4 };

/**
 * @brief MIDI note data for MIDI clips
 */
struct MidiNote {
    int noteNumber = 60;       // MIDI note number (0-127)
    int velocity = 100;        // Note velocity (0-127)
    double startBeat = 0.0;    // Start position in beats within clip
    double lengthBeats = 1.0;  // Duration in beats
    int chordGroup = 0;        // 0 = unlinked, >0 = linked to ChordAnnotation with same ID
};

/**
 * @brief Curve interpolation type for CC/PitchBend events
 */
enum class MidiCurveType : int { Step = 0, Linear = 1, Bezier = 2 };

/**
 * @brief Bezier handle offset for CC/PitchBend curve shaping
 */
struct MidiCurveHandle {
    double dx = 0.0;     // Beat offset from parent point
    double dy = 0.0;     // Normalized value offset from parent point
    bool linked = true;  // Mirror handles when one is moved
};

/**
 * @brief MIDI CC data for recorded CC events
 */
struct MidiCCData {
    int controller = 0;         // CC number (0-127)
    int value = 0;              // CC value (0-127)
    double beatPosition = 0.0;  // Position in beats within clip
    MidiCurveType curveType = MidiCurveType::Step;
    double tension = 0.0;  // -3 to +3 curve shape
    MidiCurveHandle inHandle;
    MidiCurveHandle outHandle;
};

/**
 * @brief MIDI pitch bend data for recorded pitch bend events
 */
struct MidiPitchBendData {
    int value = 0;              // 0-16383, center=8192
    double beatPosition = 0.0;  // Position in beats within clip
    MidiCurveType curveType = MidiCurveType::Step;
    double tension = 0.0;  // -3 to +3 curve shape
    MidiCurveHandle inHandle;
    MidiCurveHandle outHandle;
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

    // Beat-based position (authoritative for MIDI clips; used when autoTempo = true for Audio)
    double startBeats = 0.0;  // Start position in beats

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

    // Beat-based offset (authoritative for autoTempo clips)
    // Source beats from file start. offset (seconds) is derived: offsetBeats * 60/sourceBPM
    double offsetBeats = 0.0;

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

    // Warp marker positions (only populated when warpEnabled == true)
    struct WarpMarker {
        double sourceTime;
        double warpTime;
    };
    std::vector<WarpMarker> warpMarkers;

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
    double lengthBeats =
        4.0;  // Clip timeline length in project beats (authoritative for MIDI and autoTempo Audio)

    // Pitch
    bool autoPitch = false;
    bool analogPitch = false;  // Analog pitch: resample instead of time-stretch
    bool isAnalogPitchActive() const {
        return analogPitch && !autoTempo && !warpEnabled;
    }

    /// Whether this clip uses beat-based timing as authoritative.
    /// True for MIDI clips (always beat-based), audio clips with autoTempo,
    /// and audio clips with warp markers enabled.
    bool isBeatsAuthoritative() const {
        return type == ClipType::MIDI || autoTempo || warpEnabled;
    }
    int autoPitchMode = 0;     // 0=pitchTrack, 1=chordTrackMono, 2=chordTrackPoly
    float pitchChange = 0.0f;  // -48 to +48 semitones
    int transpose = 0;         // -24 to +24 semitones (only when !autoPitch)

    // Beat Detection
    bool autoDetectBeats = false;
    float beatSensitivity = 0.5f;

    // Playback
    bool isReversed = false;

    // Per-Clip Mix
    float volumeDB = 0.0f;  // Volume: -inf to 0 dB (clip handle)
    float gainDB = 0.0f;    // Gain: 0 to +24 dB (inspector only)
    float pan = 0.0f;       // -1.0 to 1.0

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
    std::vector<MidiCCData> midiCCData;
    std::vector<MidiPitchBendData> midiPitchBendData;

    // Chord annotations (displayed in piano roll chord row)
    struct ChordAnnotation {
        double beatPosition = 0.0;  // Position within clip (beats)
        double lengthBeats = 4.0;   // Display width (beats)
        juce::String chordName;     // Display name, e.g. "Cmaj7", "Am/E"
        int chordGroup = 0;         // 0 = unlinked, >0 = linked to notes with same ID
    };
    std::vector<ChordAnnotation> chordAnnotations;
    int nextChordGroupId = 1;  // Counter for generating unique chord group IDs
    double midiOffset = 0.0;   // User-controlled start offset in beats (playback / offset marker)
    double midiTrimOffset = 0.0;  // Left-resize trim offset in beats (content origin on timeline)

    // Groove/Shuffle/Swing (MIDI clips)
    juce::String grooveTemplate;  // TE groove template name (empty = none)
    float grooveStrength = 0.0f;  // 0.0–1.0, amount of groove to apply

    // Session view properties
    int sceneIndex = -1;  // -1 = not in session view (arrangement only)

    // Per-clip grid settings (MIDI editor)
    bool gridAutoGrid = true;
    int gridNumerator = 1;
    int gridDenominator = 4;
    bool gridSnapEnabled = true;

    // Session launch properties
    LaunchMode launchMode = LaunchMode::Trigger;
    LaunchQuantize launchQuantize = LaunchQuantize::None;

    // Constants
    static constexpr double MIN_CLIP_LENGTH = 0.1;

    // Helpers
    double getEndTime() const {
        return startTime + length;
    }

    /// Derive startTime/length from beat values using the given BPM (for MIDI clips)
    void deriveTimesFromBeats(double bpm) {
        if (bpm > 0.0) {
            if (lengthBeats > 0.0) {
                startTime = (startBeats * 60.0) / bpm;
                length = (lengthBeats * 60.0) / bpm;
            }
        }
    }

    /// Get end position in beats without BPM conversion (beats are always valid for MIDI)
    double getEndBeatsRaw() const {
        return startBeats + lengthBeats;
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

    /// TE offset in timeline seconds (source / speedRatio).
    /// TE expects offset in the same time domain as clip start (timeline seconds).
    /// Looped: phase within the loop region (offset - loopStart).
    /// Non-looped: raw trim point in the source file.
    /// For autoTempo clips, offsetBeats is authoritative and converted to
    /// timeline seconds via projectBPM at the TE boundary.
    double getTeOffset(bool looped, double projectBPM = 0.0) const {
        if (autoTempo && projectBPM > 0.0) {
            // Convert source beats to timeline seconds for TE
            if (looped)
                return (offsetBeats - loopStartBeats) * 60.0 / projectBPM;
            return offsetBeats * 60.0 / projectBPM;
        }
        if (looped)
            return sourceToTimeline(offset - loopStart);
        return sourceToTimeline(offset);
    }

    /// TE loop start in timeline seconds (source / speedRatio)
    double getTeLoopStart() const {
        return sourceToTimeline(loopStart);
    }

    /// TE loop end in timeline seconds (source / speedRatio)
    double getTeLoopEnd() const {
        return sourceToTimeline(loopStart + getSourceLength());
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
    /// Returns stored beat value for MIDI/autoTempo, calculates from time otherwise
    double getStartBeats(double bpm) const {
        if (isBeatsAuthoritative()) {
            return startBeats;  // Authoritative beat value
        }
        // Calculate from time
        return (startTime * bpm) / 60.0;
    }

    /// Get clip end position in beats (single source of truth for display)
    /// Returns start + length in beats, using authoritative values based on mode
    double getEndBeats(double bpm) const {
        if (isBeatsAuthoritative() && lengthBeats > 0.0) {
            return startBeats + lengthBeats;
        }
        // Calculate from time
        double startB = (startTime * bpm) / 60.0;
        double lenB = (length * bpm) / 60.0;
        return startB + lenB;
    }
};

}  // namespace magda
