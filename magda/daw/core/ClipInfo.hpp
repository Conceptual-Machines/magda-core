#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "ClipTypes.hpp"
#include "SourcePool.hpp"
#include "TempoMap.hpp"
#include "TempoUtils.hpp"
#include "TimeStretchModes.hpp"
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

/**
 * Fade curve type.
 *
 * PINNED: persisted in project files as these integers, and MAGDA-owned. The
 * values happen to equal tracktion::AudioFadeCurve::Type today so the engine
 * bridge is a cast; a static_assert in audio/EngineEnumPins.cpp holds that
 * equality, and if the engine ever renumbers, the bridge grows a mapping rather
 * than these values changing.
 */
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

    bool operator==(const MidiPitchExpressionPoint&) const = default;
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

    bool operator==(const MidiNote&) const = default;
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

    bool operator==(const MidiCurveHandle&) const = default;
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

    bool operator==(const MidiCCData&) const = default;
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

    bool operator==(const MidiPitchBendData&) const = default;
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

/**
 * @brief One warp marker: a point in the source pinned to a warped position.
 *
 * Both sides are seconds, unchanged from before the event split (#1901): the
 * marker moved onto the event, its units did not. Re-expressing the warp side
 * in beats would change where markers land under a tempo change, and the
 * container/content split is meant to be behaviour-neutral.
 */
struct WarpMarker {
    double sourceTime = 0.0;
    double warpTime = 0.0;

    bool operator==(const WarpMarker&) const = default;
};

/**
 * @brief Placement of a source inside a clip (#1901).
 *
 * Level 2 of the clip model. An event says which Source is heard, which part of
 * it, where inside the clip, and how the audio is interpreted. It does NOT know
 * about the timeline: its geometry is relative to the owning clip's start, and
 * the clip's bounds are a window that crops it.
 *
 * Coordinate rule: everything geometric is beats; the only source-domain values
 * are the anchor and the loop region, which are samples at the source's own
 * rate. Storing those in samples (rather than seconds or beats) is what makes a
 * source-BPM reinterpretation leave the audible region untouched and simply
 * re-read its musical length.
 */
struct AudioEvent {
    EventId id = INVALID_EVENT_ID;
    SourceId sourceId = INVALID_SOURCE_ID;

    // ---- Clip-relative geometry (beats) ------------------------------------

    /// Start relative to the owning clip's start. May be negative (the head is
    /// cropped by the clip window).
    double startBeat = 0.0;
    double lengthBeats = 0.0;

    // ---- Source domain (samples at the source's own rate) ------------------

    /// Where reading begins in the source, mapped through the warp map.
    int64_t sourceAnchorSamples = 0;

    /// Source region that repeats to fill the event: how ONE event tiles its
    /// own file. Whether it loops at all is ClipInfo::loopEnabled, which both
    /// content types share and which did not move.
    int64_t loopStartSamples = 0;
    int64_t loopLengthSamples = 0;  // 0 = derive from the event's own length

    /// Re-express the source-domain positions at a different sample rate.
    ///
    /// They are counts at the source's own rate, so whenever that rate changes
    /// under them (an unresolved source being probed, a relink onto a file at
    /// another rate, a swap onto a different source for the same audio) the
    /// same counts would otherwise mean a different instant in the file.
    void rescaleSourcePositions(double oldRate, double newRate) {
        if (oldRate <= 0.0 || newRate <= 0.0 || std::abs(newRate - oldRate) < 1.0e-9)
            return;

        const double ratio = newRate / oldRate;
        const auto scale = [ratio](int64_t samples) {
            return static_cast<int64_t>(std::llround(static_cast<double>(samples) * ratio));
        };
        sourceAnchorSamples = scale(sourceAnchorSamples);
        loopStartSamples = scale(loopStartSamples);
        loopLengthSamples = scale(loopLengthSamples);
    }

    // ---- Interpretation (seeded from the Source, then owned by the user) ---

    double interpBpm = 0.0;
    double interpTotalBeats = 0.0;
    bool interpTotalBeatsLocked = false;

    /// Musical key this event is interpreted in. Empty = unknown. keyRoot is
    /// "C" / "C#" / ... / "B"; keyScale is "major" / "minor". Inspector edits
    /// live here until the user explicitly saves them to the media library.
    std::string keyRoot;
    std::string keyScale;

    bool autoTempo = false;
    double speedRatio = 1.0;
    int timeStretchMode = 0;
    bool warpEnabled = false;
    std::vector<WarpMarker> warpMarkers;

    bool autoPitch = false;
    bool analogPitch = false;
    int autoPitchMode = 0;     // 0=pitchTrack, 1=chordTrackMono, 2=chordTrackPoly
    float pitchChange = 0.0f;  // -48 to +48 semitones
    int transpose = 0;         // -24 to +24 semitones (only when !autoPitch)

    bool reversed = false;
    bool autoDetectBeats = false;
    float beatSensitivity = 0.5f;

    bool leftChannelActive = true;
    bool rightChannelActive = true;

    // ---- Per-event mix -----------------------------------------------------

    /// Trim for this event alone. The clip's own gain sits above it.
    float gainDB = 0.0f;

    // Seconds, as before the event split. Fades are per-event now, but making
    // them tempo-relative is a behaviour change and does not belong in a
    // structural refactor.
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    int fadeInType = 1;  // FadeCurve
    int fadeOutType = 1;
    int fadeInBehaviour = 0;  // 0=gainFade, 1=speedRamp
    int fadeOutBehaviour = 0;

    bool operator==(const AudioEvent&) const = default;

    double endBeat() const {
        return startBeat + lengthBeats;
    }

    // ---- Source resolution -------------------------------------------------

    const Source* source() const {
        return SourcePool::getInstance().get(sourceId);
    }
    double sourceSampleRate() const {
        return sourceRateOf(sourceId);
    }
    juce::String sourceFilePath() const {
        return sourcePathOf(sourceId);
    }
    double sourceDurationSeconds() const {
        return sourceDurationOf(sourceId);
    }

    // ---- Source-domain conversions ----------------------------------------
    //
    // Samples are authoritative. Seconds and beats are views onto them, so a
    // change to interpBpm moves the beat view and leaves the audio alone.

    double anchorSeconds() const {
        return static_cast<double>(sourceAnchorSamples) / sourceSampleRate();
    }
    double anchorBeats() const {
        return interpBpm > 0.0 ? anchorSeconds() * interpBpm / 60.0 : 0.0;
    }
    void setAnchorSeconds(double seconds) {
        sourceAnchorSamples =
            static_cast<int64_t>(std::llround(juce::jmax(0.0, seconds) * sourceSampleRate()));
    }
    void setAnchorBeats(double beats) {
        if (interpBpm > 0.0)
            setAnchorSeconds(juce::jmax(0.0, beats) * 60.0 / interpBpm);
    }

    double loopStartSeconds() const {
        return static_cast<double>(loopStartSamples) / sourceSampleRate();
    }
    double loopLengthSeconds() const {
        return static_cast<double>(loopLengthSamples) / sourceSampleRate();
    }
    double loopStartBeats() const {
        return interpBpm > 0.0 ? loopStartSeconds() * interpBpm / 60.0 : 0.0;
    }
    double loopLengthBeats() const {
        return interpBpm > 0.0 ? loopLengthSeconds() * interpBpm / 60.0 : 0.0;
    }
    void setLoopStartSeconds(double seconds) {
        loopStartSamples =
            static_cast<int64_t>(std::llround(juce::jmax(0.0, seconds) * sourceSampleRate()));
    }
    void setLoopLengthSeconds(double seconds) {
        loopLengthSamples =
            static_cast<int64_t>(std::llround(juce::jmax(0.0, seconds) * sourceSampleRate()));
    }
    void setLoopStartBeats(double beats) {
        if (interpBpm > 0.0)
            setLoopStartSeconds(juce::jmax(0.0, beats) * 60.0 / interpBpm);
    }
    void setLoopLengthBeats(double beats) {
        if (interpBpm > 0.0)
            setLoopLengthSeconds(juce::jmax(0.0, beats) * 60.0 / interpBpm);
    }

    /// Phase of the read position within the loop region (source seconds).
    double loopPhaseSeconds() const {
        return static_cast<double>(sourceAnchorSamples - loopStartSamples) / sourceSampleRate();
    }

    // ---- Playback interpretation -------------------------------------------

    /// Analog pitch resamples instead of stretching, so it is only in force
    /// when nothing else has already forced a stretcher on.
    bool isAnalogPitchActive() const {
        return analogPitch && !autoTempo && !warpEnabled;
    }

    /// The stretch mode actually applied at playback. Leaving the mode at "Off"
    /// while the event is in beat mode, warped, sped up or pitch-shifted still
    /// stretches, using the default quality engine. UI readouts must show this,
    /// not the raw field, so the inspector and the audio editor agree.
    int getEffectiveTimeStretchMode() const {
        if (timeStretchMode == 0 && !isAnalogPitchActive() &&
            (autoTempo || warpEnabled || std::abs(speedRatio - 1.0) > 0.001 ||
             std::abs(pitchChange) > 0.001f)) {
            return time_stretch_mode::kSignalsmith;
        }
        return timeStretchMode;
    }

    /// Source seconds -> timeline seconds (speed FACTOR: faster = shorter).
    double sourceToTimeline(double sourceTime) const {
        return sourceTime / speedRatio;
    }
    /// Timeline seconds -> source seconds.
    double timelineToSource(double timelineTime) const {
        return timelineTime * speedRatio;
    }

    /// Source seconds consumed by the event: the loop region if one is set,
    /// otherwise whatever the event's own timeline extent asks for.
    double sourceLengthSeconds(double eventTimelineSeconds) const {
        return loopLengthSamples > 0 ? loopLengthSeconds() : timelineToSource(eventTimelineSeconds);
    }

    /// Offset handed to the engine, in timeline seconds. Looped: the phase
    /// within the loop region. Non-looped: the raw trim point in the source.
    ///
    /// In beat mode speedRatio is pinned to 1 and the real stretch is
    /// projectBpm / interpBpm, so the source distance has to travel through the
    /// beat domain to come out as timeline seconds.
    double engineOffsetSeconds(bool looped, double projectBpm = 0.0) const {
        if (autoTempo && isValidBpm(projectBpm)) {
            const double beats = looped ? (anchorBeats() - loopStartBeats()) : anchorBeats();
            return beats * 60.0 / projectBpm;
        }
        return sourceToTimeline(looped ? loopPhaseSeconds() : anchorSeconds());
    }
    double engineLoopStartSeconds() const {
        return sourceToTimeline(loopStartSeconds());
    }
    double engineLoopEndSeconds(double eventTimelineSeconds) const {
        return sourceToTimeline(loopStartSeconds() + sourceLengthSeconds(eventTimelineSeconds));
    }

    /// Seed the interpretation from an external analysis (Tracktion loopInfo,
    /// a detection pass). Only fills gaps, so re-analysing a file can never
    /// rewrite what the user set.
    ///
    /// numBeats is only taken when a BPM is known too: a beat count without an
    /// anchoring tempo claims musical content the file does not carry, and it
    /// renders as a plausible-looking integer that never gets corrected once a
    /// real BPM arrives.
    void seedInterpretation(double numBeats, double bpm) {
        if (bpm > 0.0 && interpBpm <= 0.0)
            interpBpm = bpm;
        if (numBeats > 0.0 && bpm > 0.0 && interpTotalBeats <= 0.0)
            interpTotalBeats = numBeats;
    }

    /// Seed interpretation from the pooled source. Only fills gaps: a value the
    /// user (or a previous seed) already set is never overwritten, so
    /// re-analysing a file cannot rewrite an interpretation.
    void seedInterpretationFromSource() {
        const auto* src = source();
        if (src == nullptr)
            return;
        if (interpBpm <= 0.0 && src->detectedBpm > 0.0)
            interpBpm = src->detectedBpm;
        if (interpTotalBeats <= 0.0 && interpBpm > 0.0 && src->durationSeconds > 0.0)
            interpTotalBeats = src->durationSeconds * interpBpm / 60.0;
        if (keyRoot.empty())
            keyRoot = src->detectedKeyRoot;
        if (keyScale.empty())
            keyScale = src->detectedKeyScale;
    }
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

    bool operator==(const AudioTake&) const = default;
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

    bool operator==(const CompSection&) const = default;
};

struct AudioClipModel {
    // Ordered list of audio events (#1901). The clip hosts them; it knows
    // nothing about files directly. Kept sorted by startBeat.
    //
    // Today nothing in the app produces more than one event: slicing, comping
    // and consolidation-without-render are the event-level features that switch
    // on later. The model, serialization and engine bridge all handle N.
    std::vector<AudioEvent> events;
    int nextEventId = 1;

    // Loop-record takes, one per pass. Empty for ordinary single-source clips.
    // When non-empty, the primary event's source mirrors
    // takes[currentTakeIndex].filePath (the active take that plays back).
    std::vector<AudioTake> takes;
    int currentTakeIndex = 0;

    // Comping. When compActive is true the clip plays a rendered composite
    // (the primary event's source is the comp render) assembled from `comp`,
    // which assigns a take to each region of the comp timeline. Empty = no comp.
    //
    // Comping keeps its render-based shape for now; turning takes into event
    // lanes and dropping the render is the follow-up to #1901, not part of the
    // schema split.
    std::vector<CompSection> comp;
    bool compActive = false;

    bool operator==(const AudioClipModel&) const = default;

    AudioEvent* primaryEvent() {
        return events.empty() ? nullptr : &events.front();
    }
    const AudioEvent* primaryEvent() const {
        return events.empty() ? nullptr : &events.front();
    }

    AudioEvent* findEvent(EventId eventId) {
        for (auto& event : events)
            if (event.id == eventId)
                return &event;
        return nullptr;
    }
    const AudioEvent* findEvent(EventId eventId) const {
        for (const auto& event : events)
            if (event.id == eventId)
                return &event;
        return nullptr;
    }

    /// Append an event with a freshly allocated id and return it.
    ///
    /// The reference is into `events`, so a later addEvent invalidates it.
    /// Hold the id, not the reference, across calls.
    AudioEvent& addEvent(AudioEvent event) {
        event.id = nextEventId++;
        events.push_back(std::move(event));
        return events.back();
    }
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

    bool operator==(const MidiTake&) const = default;
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

    bool operator==(const MidiCompSection&) const = default;
};

struct MidiClipModel {
    juce::String sourceFilePath;

    // Loop-record takes, one per pass. Empty for ordinary single-pass clips.
    // When non-empty, the active take's events are mirrored into the clip's
    // authoritative ClipInfo::midiNotes / midiCCData / midiPitchBendData (the
    // rendered, engine-synced content) — mirroring how AudioClipModel fronts
    // takes[currentTakeIndex] into the primary event's source.
    std::vector<MidiTake> takes;
    int currentTakeIndex = 0;

    // Comping. When compActive, the authoritative event vectors are assembled
    // from `comp` (each section assigns a take to a beat range) instead of a
    // single take. Empty comp = no comp.
    std::vector<MidiCompSection> comp;
    bool compActive = false;

    bool operator==(const MidiClipModel&) const = default;
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

    // Ghost clips. Clips sharing a non-zero linkGroupId mirror their
    // content-defining fields (see copySharedContentFrom): editing any member
    // updates all of them. Symmetric — there is no "original". 0 = unlinked.
    // A group with a single remaining member is inert (no propagation, no
    // ghost visuals); groups are never auto-dissolved so delete-undo restores
    // membership cleanly.
    int linkGroupId = 0;

    // Timeline position. This is the canonical placement model for every clip type.
    ClipPlacement placement;

    // Enable/disable toggle (#1736). Disabled clips do not play — synced to
    // te::Clip::disabled, which excludes the clip from the playback graph.
    // Per-instance (NOT ghost-shared): disabling one link-group member must
    // not silence its siblings.
    bool enabled = true;

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

    // =========================================================================
    // Event access (audio clips)
    //
    // Phase A of #1901 never builds a clip with more than one event, so
    // primaryEvent() is the whole story for existing call sites. It becomes
    // "the event the user is editing" once the UI gains event selection; it is
    // not a permanent stand-in for iterating events.
    // =========================================================================

    AudioEvent* primaryEvent() {
        return isAudio() ? audio().primaryEvent() : nullptr;
    }
    const AudioEvent* primaryEvent() const {
        return isAudio() ? audio().primaryEvent() : nullptr;
    }

    /// Every audio event. Audio clips only: this is a checked variant access
    /// and throws for MIDI, deliberately, so a caller that has not established
    /// the content type fails loudly instead of writing into a discarded list.
    /// Use primaryEvent(), which returns nullptr for MIDI, when unsure.
    std::vector<AudioEvent>& events() {
        return audio().events;
    }
    const std::vector<AudioEvent>& events() const {
        return audio().events;
    }

    // =========================================================================
    // Looping
    //
    // Unchanged by #1901. The toggle is shared by both content types, and
    // loopStartBeats / loopLengthBeats are the MIDI loop in clip beats. Only
    // an audio clip's source REGION moved, onto its event, because that is a
    // position in a file rather than on the timeline.
    // =========================================================================

    bool loopEnabled = false;
    double loopStartBeats = 0.0;
    double loopLengthBeats = 0.0;

    /// Loop start in TIMELINE beats for whichever content the clip holds.
    /// Same domain rules as loopLengthInBeats below.
    double loopStartInBeats(double projectBpm) const {
        const auto* event = primaryEvent();
        if (event == nullptr)
            return loopStartBeats;

        if (event->autoTempo)
            return event->loopStartBeats();

        if (!isValidBpm(projectBpm))
            return 0.0;

        return event->sourceToTimeline(event->loopStartSeconds()) * projectBpm / 60.0;
    }

    /// Loop length in TIMELINE beats for whichever content the clip holds.
    ///
    /// A MIDI clip keeps it in the field above, already in timeline beats. An
    /// audio clip's loop is a source region, and the two domains only coincide
    /// under beat mode, where the source is stretched onto the project grid.
    /// Off beat mode the region plays at its own rate, so its timeline span
    /// comes from seconds and the project tempo. AudioEvent::loopLengthBeats()
    /// is the SOURCE-beat view and is not interchangeable with this.
    double loopLengthInBeats(double projectBpm) const {
        const auto* event = primaryEvent();
        if (event == nullptr)
            return loopLengthBeats;

        if (event->autoTempo)
            return event->loopLengthBeats();

        if (!isValidBpm(projectBpm))
            return 0.0;

        return event->sourceToTimeline(event->loopLengthSeconds()) * projectBpm / 60.0;
    }

    // =========================================================================
    // Clip-level placement and mix
    //
    // How the audio is read and interpreted lives on the events. What stays
    // here belongs to the container: where it sits, whether it plays, how it is
    // launched, and its own gain.
    // =========================================================================

    double lengthBeats = 4.0;  // Transitional mirror of placement.lengthBeats

    // Per-Clip Mix
    float volumeDB = 0.0f;  // Volume: -inf to 0 dB (clip handle)
    float gainDB = 0.0f;    // Gain: 0 to +24 dB (inspector only)
    float pan = 0.0f;       // -1.0 to 1.0

    // Crossfade with the neighbouring clip on the same track. Crossfades
    // BETWEEN events inside one clip are an event-level concern.
    bool autoCrossfade = false;

    // launchFadeSamples: ramp on the stopped→playing transition. Default 256
    // matches TE's prior hard-coded behaviour; 0 preserves the leading transient.
    int launchFadeSamples = 256;

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

        bool operator==(const ChordAnnotation&) const = default;
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
        syncSingleEventToClipBounds();
    }

    /// Keep a single-event clip's content coextensive with its container.
    ///
    /// Phase A of #1901 splits the schema without changing behaviour, so a clip
    /// and the one event inside it stay the same extent and every existing
    /// resize/trim path keeps working unchanged. Clips holding several events
    /// are already representable, and for those the clip bounds are a window
    /// that crops rather than resizes, so this deliberately does nothing.
    void syncSingleEventToClipBounds() {
        if (!isAudio())
            return;
        auto& list = audio().events;
        if (list.size() != 1)
            return;
        list.front().startBeat = 0.0;
        list.front().lengthBeats = placement.lengthBeats;
    }

    /// Event fields a ghost sibling mirrors: what the source IS and how it is
    /// interpreted. The placement-coupled fields (anchor, loop region,
    /// speedRatio, fades, channels, gain, and the event's own geometry) stay
    /// per-instance, so one ghost's resize or trim cannot corrupt its siblings.
    /// Kept in lockstep with sharedEventFieldsEqual below.
    static void copySharedEventFieldsFrom(AudioEvent& dst, const AudioEvent& src) {
        dst.sourceId = src.sourceId;
        dst.interpBpm = src.interpBpm;
        dst.interpTotalBeats = src.interpTotalBeats;
        dst.interpTotalBeatsLocked = src.interpTotalBeatsLocked;
        dst.keyRoot = src.keyRoot;
        dst.keyScale = src.keyScale;
        dst.autoTempo = src.autoTempo;
        dst.timeStretchMode = src.timeStretchMode;
        dst.warpEnabled = src.warpEnabled;
        dst.warpMarkers = src.warpMarkers;
        dst.autoPitch = src.autoPitch;
        dst.analogPitch = src.analogPitch;
        dst.autoPitchMode = src.autoPitchMode;
        dst.pitchChange = src.pitchChange;
        dst.transpose = src.transpose;
        dst.reversed = src.reversed;
        dst.autoDetectBeats = src.autoDetectBeats;
        dst.beatSensitivity = src.beatSensitivity;
    }

    static bool sharedEventFieldsEqual(const AudioEvent& a, const AudioEvent& b) {
        return a.sourceId == b.sourceId && a.interpBpm == b.interpBpm &&
               a.interpTotalBeats == b.interpTotalBeats &&
               a.interpTotalBeatsLocked == b.interpTotalBeatsLocked && a.keyRoot == b.keyRoot &&
               a.keyScale == b.keyScale && a.autoTempo == b.autoTempo &&
               a.timeStretchMode == b.timeStretchMode && a.warpEnabled == b.warpEnabled &&
               a.warpMarkers == b.warpMarkers && a.autoPitch == b.autoPitch &&
               a.analogPitch == b.analogPitch && a.autoPitchMode == b.autoPitchMode &&
               a.pitchChange == b.pitchChange && a.transpose == b.transpose &&
               a.reversed == b.reversed && a.autoDetectBeats == b.autoDetectBeats &&
               a.beatSensitivity == b.beatSensitivity;
    }

    /// Copy the content-defining fields from a link-group sibling (ghost-clip
    /// mirroring). Everything that says WHAT the clip contains and how the
    /// source is interpreted is shared — including the name (the UI appends a
    /// per-instance #index for display); everything that says WHERE it sits
    /// and how it mixes stays per-instance: id, trackId, colour, view,
    /// placement (+ derived mirrors), volume/gain/pan, launch and grid
    /// settings, plus the per-instance event fields listed above.
    ///
    /// When the two clips disagree on how many events they hold, the sibling's
    /// event list is taken wholesale: the structure itself changed, and there
    /// is no per-instance state on an event that does not exist yet.
    void copySharedContentFrom(const ClipInfo& src) {
        name = src.name;

        if (isAudio() && src.isAudio() && events().size() == src.events().size()) {
            for (size_t i = 0; i < events().size(); ++i)
                copySharedEventFieldsFrom(events()[i], src.events()[i]);
            auto& dstAudio = audio();
            const auto& srcAudio = src.audio();
            // Ids stay per-instance, so the allocator must never step back
            // behind one already handed out here. Equal today because every
            // live flow keeps link-group members at one event; once slicing
            // lets their histories diverge, taking the source's counter
            // verbatim could mint a duplicate on the next addEvent.
            dstAudio.nextEventId = juce::jmax(dstAudio.nextEventId, srcAudio.nextEventId);
            dstAudio.takes = srcAudio.takes;
            dstAudio.currentTakeIndex = srcAudio.currentTakeIndex;
            dstAudio.comp = srcAudio.comp;
            dstAudio.compActive = srcAudio.compActive;
        } else {
            // Wholesale: this replaces every per-instance event field (anchor,
            // region, speedRatio, fades, channels, gain, geometry) and adopts
            // the source's event ids. Only reachable when the event structure
            // itself differs, which no Phase A flow produces. Slicing will:
            // it needs a prefix match here, not a swap.
            jassert(!isAudio() || !src.isAudio() || events().size() == src.events().size());
            content = src.content;
        }

        midiNotes = src.midiNotes;
        midiCCData = src.midiCCData;
        midiPitchBendData = src.midiPitchBendData;
        chordAnnotations = src.chordAnnotations;
        nextChordGroupId = src.nextChordGroupId;
        grooveTemplate = src.grooveTemplate;
        grooveStrength = src.grooveStrength;
    }

    /// True when the content-defining fields are identical: the exact set
    /// copySharedContentFrom mirrors, kept in lockstep with it. Lets the
    /// ghost-clip propagation skip the deep sibling copies (and the sibling
    /// notifications) when an edit only touched per-instance state.
    bool sharedContentEquals(const ClipInfo& src) const {
        if (name != src.name || midiNotes != src.midiNotes || midiCCData != src.midiCCData ||
            midiPitchBendData != src.midiPitchBendData ||
            chordAnnotations != src.chordAnnotations || nextChordGroupId != src.nextChordGroupId ||
            grooveTemplate != src.grooveTemplate || grooveStrength != src.grooveStrength) {
            return false;
        }

        if (!isAudio() || !src.isAudio())
            return content == src.content;

        const auto& a = audio();
        const auto& b = src.audio();
        if (a.events.size() != b.events.size() || a.nextEventId != b.nextEventId ||
            a.takes != b.takes || a.currentTakeIndex != b.currentTakeIndex || a.comp != b.comp ||
            a.compActive != b.compActive) {
            return false;
        }
        for (size_t i = 0; i < a.events.size(); ++i)
            if (!sharedEventFieldsEqual(a.events[i], b.events[i]))
                return false;
        return true;
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

    /// Get end position in beats without BPM conversion (beats are always valid for MIDI)
    double getEndBeatsRaw() const {
        return placement.endBeat();
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
        if (loopEnabled && isValidBpm(projectBPM)) {
            // MIDI keeps its loop length in clip beats; an audio clip's is the
            // beat view of its event's source region.
            const auto* event = primaryEvent();
            const double beats = event != nullptr ? event->loopLengthBeats() : loopLengthBeats;
            if (beats > 0.0)
                return beats * 60.0 / projectBPM;
        }
        return getTimelineLength(projectBPM);
    }
};

/// Primary audio event of a clip, null-safe on both the clip pointer and the
/// clip's content type. Most call sites hold a `ClipInfo*` from a lookup that
/// can miss, and MIDI clips have no events at all.
inline AudioEvent* primaryEventOf(ClipInfo* clip) {
    return clip != nullptr ? clip->primaryEvent() : nullptr;
}
inline const AudioEvent* primaryEventOf(const ClipInfo* clip) {
    return clip != nullptr ? clip->primaryEvent() : nullptr;
}

/// Read-only view of a clip's audio event for code that only ever reads.
///
/// Phase A of #1901 gives every audio clip exactly one event spanning it, so
/// readers that used to reach for a clip-level audio field can reach for this
/// instead. A MIDI clip yields a default event, which keeps those readers total
/// without a null branch. Anything that needs to handle several events per clip
/// iterates ClipInfo::events() rather than calling this.
inline const AudioEvent& audioEventRef(const ClipInfo& clip) {
    static const AudioEvent kNoEvent{};
    const auto* event = clip.primaryEvent();
    return event != nullptr ? *event : kNoEvent;
}

}  // namespace magda
