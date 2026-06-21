#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "ClipTypes.hpp"
#include "TempoMap.hpp"
#include "TempoUtils.hpp"
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
 * @brief Per-note pitch expression point (MPE pitch glide)
 *
 * Beat position is relative to the note's start. Value is a pitch offset in
 * semitones from the note's base pitch (MPE pitchbend, ±48 semitone range —
 * matches Tracktion Engine's fixed MPE conversion range).
 */
struct MidiPitchExpressionPoint {
    double beat = 0.0;       // Position relative to note start (0..note length)
    double semitones = 0.0;  // Pitch offset in semitones (-48..+48)
};

/**
 * @brief MIDI note data for MIDI clips
 */
struct MidiNote {
    int noteNumber = 60;       // MIDI note number (0-127)
    int velocity = 100;        // Note velocity (0-127)
    double startBeat = 0.0;    // Start position in beats within clip
    double lengthBeats = 1.0;  // Duration in beats
    int chordGroup = 0;        // 0 = unlinked, >0 = linked to ChordAnnotation with same ID

    // Per-note pitch glide (MPE). Sorted by beat. Empty = no expression.
    std::vector<MidiPitchExpressionPoint> pitchExpression;

    bool hasPitchExpression() const {
        return !pitchExpression.empty();
    }
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
 * @brief Clip placement on a musical timeline.
 *
 * This is content-agnostic: audio, MIDI, automation, and future clip-like
 * objects all occupy a project beat range. Audio source offsets and loop
 * regions are separate source-domain data.
 */
struct ClipPlacement {
    double startBeat = 0.0;
    double lengthBeats = 4.0;

    double endBeat() const {
        return startBeat + lengthBeats;
    }
};

struct AudioSourceFacts {
    juce::String filePath;
    double durationSeconds = 0.0;
};

struct AudioSourceInterpretation {
    double bpm = 0.0;
    double totalBeats = 0.0;
    bool totalBeatsLocked = false;
    // Musical key the source is interpreted in. Optional — empty = unknown.
    // Inspector/editor edits live on the clip until the user explicitly saves
    // them to the media library. keyScale is "major" / "minor" / "" when
    // unknown; keyRoot is "C" / "C#" / ... / "B" or empty.
    std::string keyRoot;
    std::string keyScale;
};

/**
 * @brief One loop-record take: a single recorded pass over the loop range.
 *
 * Loop recording captures each pass as its own audio file (Tracktion splits the
 * continuous recording at the loop boundaries). filePath is the on-disk source
 * for that pass; durationSeconds is its audio length.
 *
 * Takes have no per-take time offset: they are loop-aligned alternatives that
 * all share the clip start (take 0 at t=0). Recording before the loop with Loop
 * on therefore does not preserve the pre-loop lead-in as a take; the clip is
 * loop-aligned. Supporting a lead-in would require a per-take offset here and in
 * the comp model.
 */
struct AudioTake {
    juce::String filePath;
    double durationSeconds = 0.0;
};

/**
 * @brief One comp section: the take that plays over [startSeconds, endSeconds).
 *
 * Comp sections tile the comp timeline (source-domain seconds, take 0 at t=0).
 * They are kept sorted and contiguous; a comp is the ordered list of sections.
 * takeIndex points into AudioClipModel::takes.
 */
struct CompSection {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int takeIndex = 0;
};

struct AudioClipModel {
    AudioSourceFacts source;
    AudioSourceInterpretation interpretation;

    // Loop-record takes, one per pass. Empty for ordinary single-source clips.
    // When non-empty, source.filePath mirrors takes[currentTakeIndex].filePath
    // (the active take that plays back).
    std::vector<AudioTake> takes;
    int currentTakeIndex = 0;

    // Comping. When compActive is true the clip plays a rendered composite
    // (source.filePath points at the comp render) assembled from `comp`, which
    // assigns a take to each region of the comp timeline. Empty comp = no comp.
    std::vector<CompSection> comp;
    bool compActive = false;
};

/**
 * @brief One MIDI loop-record take: a single recorded pass over the loop range.
 *
 * The MIDI counterpart of AudioTake. Where audio takes are file references,
 * MIDI takes are full note/controller snapshots — assembling a comp needs no
 * render, just picking which take's events play. Immutable once recorded.
 */
struct MidiTake {
    std::vector<MidiNote> notes;
    std::vector<MidiCCData> cc;
    std::vector<MidiPitchBendData> pitchBend;
};

/**
 * @brief One MIDI comp section: the take that plays over [startBeat, endBeat).
 *
 * The beats-domain counterpart of CompSection. Sections tile the comp timeline
 * (take 0 at beat 0), kept sorted and contiguous; a comp is the ordered list.
 * takeIndex points into MidiClipModel::takes. Unlike audio there is no render —
 * the active note list is assembled directly from the sections + take note sets.
 */
struct MidiCompSection {
    double startBeat = 0.0;
    double endBeat = 0.0;
    int takeIndex = 0;
};

struct MidiClipModel {
    juce::String sourceFilePath;

    // Loop-record takes, one per pass. Empty for ordinary single-pass clips.
    // When non-empty, the active take's events are mirrored into the clip's
    // authoritative ClipInfo::midiNotes / midiCCData / midiPitchBendData (the
    // rendered, engine-synced content) — mirroring how AudioClipModel fronts
    // takes[currentTakeIndex] into source.filePath.
    std::vector<MidiTake> takes;
    int currentTakeIndex = 0;

    // Comping. When compActive, the authoritative event vectors are assembled
    // from `comp` (each section assigns a take to a beat range) instead of a
    // single take. Empty comp = no comp.
    std::vector<MidiCompSection> comp;
    bool compActive = false;
};

using ClipContent = std::variant<MidiClipModel, AudioClipModel>;

/**
 * @brief Clip data structure containing all clip properties
 */
struct ClipInfo {
    ClipId id = INVALID_CLIP_ID;
    TrackId trackId = INVALID_TRACK_ID;
    juce::String name;
    juce::Colour colour;
    ClipView view = ClipView::Arrangement;  // Which view this clip belongs to
    ClipContent content = MidiClipModel{};

    // Timeline position. This is the canonical placement model for every clip type.
    ClipPlacement placement;

    ClipType getType() const {
        return std::holds_alternative<AudioClipModel>(content) ? ClipType::Audio : ClipType::MIDI;
    }

    bool isAudio() const {
        return std::holds_alternative<AudioClipModel>(content);
    }

    bool isMidi() const {
        return std::holds_alternative<MidiClipModel>(content);
    }

    AudioClipModel& audio() {
        return std::get<AudioClipModel>(content);
    }

    const AudioClipModel& audio() const {
        return std::get<AudioClipModel>(content);
    }

    MidiClipModel& midi() {
        return std::get<MidiClipModel>(content);
    }

    const MidiClipModel& midi() const {
        return std::get<MidiClipModel>(content);
    }

    void setAudioContent() {
        content = AudioClipModel{};
    }

    void setMidiContent() {
        content = MidiClipModel{};
    }

    /// Front MIDI take `idx`: copy its events into the authoritative active
    /// note/CC/pitchbend vectors and mark it current. No-op unless this is a
    /// MIDI clip with that take. Mirrors how audio fronts takes[idx] into the
    /// clip source.
    void frontMidiTake(int idx) {
        if (!isMidi())
            return;
        auto& m = midi();
        if (idx < 0 || idx >= static_cast<int>(m.takes.size()))
            return;
        m.currentTakeIndex = idx;
        midiNotes = m.takes[static_cast<size_t>(idx)].notes;
        midiCCData = m.takes[static_cast<size_t>(idx)].cc;
        midiPitchBendData = m.takes[static_cast<size_t>(idx)].pitchBend;
    }

    // Transient UI: whether the loop-record take lanes are expanded in the
    // waveform editor (collapsed = the normal single active-take waveform).
    // Not serialized.
    bool takesExpanded = true;

    // Derived timeline seconds cache. Kept only for bridge/UI call sites that
    // have not moved to beats yet; do not treat these as model authority.
    double startTime = 0.0;
    double length = 4.0;

    // Transitional mirrors for call sites that still access beat fields directly.
    // Keep in sync via setPlacementBeats / deriveTimesFromBeats while the refactor
    // removes direct field access.
    double startBeats = 0.0;

    /// Populate source metadata from engine (only sets if not already populated).
    /// This is source-domain data only; it must never edit clip placement or
    /// source loop region state.
    ///
    /// numBeats is only seeded when bpm is also known. A beat count without an
    /// anchoring BPM is meaningless — it claims musical content the file
    /// doesn't actually carry. Without this guard the source-domain
    /// interpretation gets a bpm=0 / totalBeats=projectBpm×duration seed,
    /// which renders as "—" in the BPM column but a wrong integer in the
    /// beats column, and stays that way after the user later sets the real
    /// BPM unless the BPM-edit path also rewrites totalBeats.
    void setSourceMetadata(double numBeats, double bpm) {
        if (!isAudio())
            return;

        auto& source = audio();
        if (bpm > 0.0 && source.interpretation.bpm <= 0.0)
            source.interpretation.bpm = bpm;
        if (numBeats > 0.0 && bpm > 0.0 && source.interpretation.totalBeats <= 0.0)
            source.interpretation.totalBeats = numBeats;
        if (source.source.durationSeconds <= 0.0 && source.interpretation.totalBeats > 0.0 &&
            source.interpretation.bpm > 0.0) {
            source.source.durationSeconds =
                source.interpretation.totalBeats * 60.0 / source.interpretation.bpm;
        }
    }

    // =========================================================================
    // Audio playback parameters (TE-aligned terminology)
    // =========================================================================

    // Source offset - where to start reading from source file
    // TE: Clip::offset (but TE stores in stretched time, we use source time)
    double offset = 0.0;  // Start position in source file (source-time seconds)

    // Beat-based offset (authoritative for autoTempo clips)
    // Source beats from file start. offset (seconds) is derived: offsetBeats * 60/source
    // interpretation BPM
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
    // Auto-tempo / Musical mode
    // =========================================================================
    // When autoTempo=true:
    // - Beat values are authoritative, time values are derived from BPM
    // - TE's autoTempo is enabled, clips maintain fixed musical length
    // - speedRatio must be 1.0 (TE requirement)
    // When autoTempo=false:
    // - Timeline placement is still beat-domain
    // - Source playback uses offset/loop/source seconds without implying timeline position
    bool autoTempo = false;  // Enable beat-based length (musical mode)

    // Beat-based loop properties (only used when autoTempo = true)
    // TE: AudioClipBase::loopStartBeats, loopLengthBeats
    double loopStartBeats = 0.0;   // Loop start in beats (relative to file start)
    double loopLengthBeats = 0.0;  // Loop length in beats (0 = derive from clip length)
    double lengthBeats = 4.0;      // Transitional mirror of placement.lengthBeats

    // Pitch
    bool autoPitch = false;
    bool analogPitch = false;  // Analog pitch: resample instead of time-stretch
    bool isAnalogPitchActive() const {
        return analogPitch && !autoTempo && !warpEnabled;
    }

    // The time-stretch mode that is actually applied at playback. When the mode
    // is left at "Off" (0) but the clip is in beat mode, warped, sped up, or
    // pitch-shifted (without analog pitch), TE silently stretches using its
    // default SoundTouch HQ engine. UI readouts must show this effective mode,
    // not the raw field, so the inspector and the audio editor agree — e.g.
    // after a session drop auto-enables beat mode. (4 = soundtouchBetter.)
    int getEffectiveTimeStretchMode() const {
        if (timeStretchMode == 0 && !isAnalogPitchActive() &&
            (autoTempo || warpEnabled || std::abs(speedRatio - 1.0) > 0.001 ||
             std::abs(pitchChange) > 0.001f)) {
            return 4;  // soundtouchBetter (TE's defaultMode)
        }
        return timeStretchMode;
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

    // launchFadeSamples: ramp on the stopped→playing transition. Default 256
    // matches TE's prior hard-coded behaviour; 0 preserves the leading transient.
    int launchFadeSamples = 256;

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
    static constexpr int DEFAULT_MIDI_EDITOR_ROW_HEIGHT = 12;
    static constexpr int MIN_MIDI_EDITOR_ROW_HEIGHT = 6;
    static constexpr int MAX_MIDI_EDITOR_ROW_HEIGHT = 40;

    bool gridAutoGrid = true;
    int gridNumerator = 1;
    int gridDenominator = 4;
    bool gridSnapEnabled = true;
    int midiEditorRowHeight = 0;  // 0 = editor default

    // Session launch properties
    LaunchMode launchMode = LaunchMode::Trigger;
    LaunchQuantize launchQuantize = LaunchQuantize::OneBar;
    FollowAction followAction = FollowAction::None;
    double followActionDelayBeats = 0.0;
    int followActionLoopCount = 1;

    // Per-clip playhead position (seconds, looped).
    // Updated by SessionClipScheduler from audio-thread data.
    // -1.0 = not playing.
    double sessionPlayheadPos = -1.0;

    // Constants
    static constexpr double MIN_CLIP_LENGTH = 0.1;

    // Helpers
    void setPlacementBeats(double startBeat, double beatLength) {
        placement.startBeat = juce::jmax(0.0, startBeat);
        placement.lengthBeats = juce::jmax(0.0, beatLength);
        startBeats = placement.startBeat;
        lengthBeats = placement.lengthBeats;
    }

    /// Derive startTime/length from placement beats using the given BPM.
    void deriveTimesFromBeats(double bpm) {
        if (isValidBpm(bpm)) {
            if (placement.lengthBeats <= 0.0 && lengthBeats > 0.0)
                setPlacementBeats(startBeats, lengthBeats);
            if (placement.lengthBeats > 0.0) {
                startTime = (placement.startBeat * 60.0) / bpm;
                length = (placement.lengthBeats * 60.0) / bpm;
            }
        }
    }

    // =========================================================================
    // Beats-first source-domain setters (audio clips)
    //
    // The ONLY supported paths for writing offset / loopStart / loopLength.
    // They take BEATS and derive the seconds mirror via the source's
    // interpretation BPM (during the transitional period while the seconds
    // fields still exist). When all readers are migrated off the seconds
    // fields, those fields go away and the derive step goes with them.
    //
    // Deliberately no `setXxxSeconds` counterpart: a parallel seconds API
    // would just keep call sites writing seconds and quietly recompute beats,
    // which is the leak we're trying to close. Callers that have seconds
    // convert at the call site: beats = seconds × interpBpm / 60.
    // =========================================================================

    void setSourceOffsetBeats(double beats, double interpBpm) {
        offsetBeats = juce::jmax(0.0, beats);
        if (interpBpm > 0.0)
            offset = offsetBeats * 60.0 / interpBpm;
    }

    void setLoopStartBeats(double beats, double interpBpm) {
        loopStartBeats = juce::jmax(0.0, beats);
        if (interpBpm > 0.0)
            loopStart = loopStartBeats * 60.0 / interpBpm;
    }

    void setLoopLengthBeats(double beats, double interpBpm) {
        loopLengthBeats = juce::jmax(0.0, beats);
        if (interpBpm > 0.0)
            loopLength = loopLengthBeats * 60.0 / interpBpm;
    }

    /// Get end position in beats without BPM conversion (beats are always valid for MIDI)
    double getEndBeatsRaw() const {
        return placement.endBeat();
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

    /// Effective source length: loopLength if set, otherwise derived from timeline placement.
    double getSourceLength(double projectBPM) const {
        return loopLength > 0.0 ? loopLength : timelineToSource(getTimelineLength(projectBPM));
    }

    /// Compatibility fallback for callers that still do not have project BPM nearby.
    double getSourceLength() const {
        return loopLength > 0.0 ? loopLength : timelineToSource(length);
    }

    /// Source length expressed in timeline seconds
    double getSourceLengthOnTimeline(double projectBPM) const {
        return sourceToTimeline(getSourceLength(projectBPM));
    }

    /// Compatibility fallback for callers that still do not have project BPM nearby.
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
        if (autoTempo && isValidBpm(projectBPM)) {
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
    double getTeLoopEnd(double projectBPM) const {
        return sourceToTimeline(loopStart + getSourceLength(projectBPM));
    }

    /// Compatibility fallback for callers that still do not have project BPM nearby.
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
        juce::ignoreUnused(bpm);
        return placement.lengthBeats;
    }

    /// Set clip length from beats (updates placement and derived seconds cache)
    void setLengthFromBeats(double beats, double bpm) {
        setPlacementBeats(placement.startBeat, beats);
        deriveTimesFromBeats(bpm);
    }

    /// Get clip start position in project beats.
    double getStartBeats(double bpm) const {
        juce::ignoreUnused(bpm);
        return placement.startBeat;
    }

    /// Get clip end position in project beats.
    double getEndBeats(double bpm) const {
        juce::ignoreUnused(bpm);
        return placement.endBeat();
    }

    // =========================================================================
    // Robust seconds accessors (issue #1157)
    //
    // For autoTempo audio clips and MIDI clips, beats are AUTHORITATIVE — the
    // seconds fields (length, startTime, offset, loopStart, loopLength) are
    // derived caches that go stale every time projectBPM or source interpretation BPM changes.
    // Renderers, sync code, and inspector readouts that go through these
    // accessors compute the live value from beats and never depend on cache
    // freshness. The cached fields are still maintained (so non-migrated
    // readers stay correct), but new code should prefer the accessors.
    // =========================================================================

    /// Timeline-domain seconds for the clip's length, derived from placement.
    double getTimelineLength(double projectBPM) const {
        if (placement.lengthBeats > 0.0 && isValidBpm(projectBPM)) {
            return placement.lengthBeats * 60.0 / projectBPM;
        }
        return length;
    }

    /// Timeline-domain seconds for the clip's start position, derived from placement.
    double getTimelineStart(double projectBPM) const {
        if (isValidBpm(projectBPM)) {
            return placement.startBeat * 60.0 / projectBPM;
        }
        return startTime;
    }

    /// Timeline-domain end position (start + length).
    double getTimelineEnd(double projectBPM) const {
        return getTimelineStart(projectBPM) + getTimelineLength(projectBPM);
    }

    // ----- Position-aware overloads (tempo single-source-of-truth) -----
    // These walk the tempo curve via the facade, so they stay correct under a
    // varying tempo where `beats * 60 / bpm` would drift. Beats are
    // authoritative; placement.startBeat / endBeat() drive the result.

    /// Timeline-domain seconds for the clip's start position.
    double getTimelineStart(const TempoMap& tempoMap) const {
        return tempoMap.beatToTime(placement.startBeat);
    }

    /// Timeline-domain seconds for the clip's length. Position-aware: a beat
    /// span occupies different wall-clock seconds depending on where it sits on
    /// the tempo curve, so length = end-time minus start-time (not a direct
    /// lengthBeats conversion).
    double getTimelineLength(const TempoMap& tempoMap) const {
        return tempoMap.beatToTime(placement.endBeat()) - tempoMap.beatToTime(placement.startBeat);
    }

    /// Timeline-domain end position.
    double getTimelineEnd(const TempoMap& tempoMap) const {
        return tempoMap.beatToTime(placement.endBeat());
    }

    /// Timeline-domain seconds for the looping playback span — the length the
    /// session playhead sweeps before it wraps. A looping clip wraps at its loop
    /// length, which is the basis SessionClipScheduler uses for the playhead
    /// position. This diverges from getTimelineLength() after a source-BPM
    /// reinterpretation changes loopLengthBeats without touching
    /// placement.lengthBeats; use this (not getTimelineLength) for the slot
    /// progress overlay so the bar and the playhead stay consistent.
    double getTimelineLoopLength(double projectBPM) const {
        if (loopEnabled && loopLengthBeats > 0.0 && isValidBpm(projectBPM)) {
            return loopLengthBeats * 60.0 / projectBPM;
        }
        return getTimelineLength(projectBPM);
    }

    /// Source-domain seconds for the loop start. For autoTempo clips, computed
    /// live from loopStartBeats × 60 / source interpretation BPM. For non-autoTempo clips,
    /// returns the stored field.
    double getSourceLoopStart() const {
        if (autoTempo && audio().interpretation.bpm > 0.0) {
            return loopStartBeats * 60.0 / audio().interpretation.bpm;
        }
        return loopStart;
    }

    /// Source-domain seconds for the loop length.
    double getSourceLoopLength() const {
        if (autoTempo && audio().interpretation.bpm > 0.0 && loopLengthBeats > 0.0) {
            return loopLengthBeats * 60.0 / audio().interpretation.bpm;
        }
        return loopLength;
    }

    /// Source-domain seconds for the read-position offset.
    double getSourceOffset() const {
        if (autoTempo && audio().interpretation.bpm > 0.0) {
            return offsetBeats * 60.0 / audio().interpretation.bpm;
        }
        return offset;
    }
};

}  // namespace magda
