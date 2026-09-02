#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "clip/GrooveTemplate.hpp"
#include "clip/MidiEventList.hpp"
#include "clip/WarpMap.hpp"
#include "core/ClipInfo.hpp"
#include "core/TypeIds.hpp"
#include "transport/TimeDomains.hpp"

/**
 * @file ClipSnapshot.hpp
 * @brief What every track plays, resolved, as one immutable value.
 *
 * Clip positions are deliberately not in the render plan (see RenderPlan.hpp):
 * a plan is topology, and moving a clip is not a topology change. Clips reach
 * the audio thread through this instead, swapped in whole the way a plan is, so
 * an edit to a clip never recompiles a plan.
 *
 * Resolved is the point of it. Occlusion and crossfades are worked out once, at
 * compile time, by the model functions that already own those rules
 * (computeAudibleSpans, effectiveFadesIn), so a `ClipAudio` or `ClipMidi` op
 * never sees an overlap: it plays a span with fades. An edit recompiles this the
 * way a structural change recompiles a plan, which is why there is no sync
 * protocol between two representations of a lane and no state that can go stale
 * (#2003).
 *
 * Both faces of every instant are carried, beats and seconds. Beats are the
 * model's authority and what auto tempo and warp keep working in; the seconds
 * are derived here, once, through the tempo map, because a source is handed
 * seconds (ClipPlacement.hpp) and there must not be a second tempo map on the
 * audio thread. The transport publishes both faces of its cursor for the same
 * reason; that the two were derived through the same map is what the
 * fingerprint below is for.
 *
 * Not keyed to a plan. Everything here is keyed by TrackId, so unlike PlanValues
 * it carries no plan fingerprint and a plan swap does not invalidate it. It does
 * carry the fingerprint of the tempo map its seconds were derived through: a
 * tempo edit moves every one of them, so a snapshot compiled against another map
 * is stale in a way nothing downstream could otherwise notice.
 */

namespace magda::engine {

/**
 * @brief A stretch of timeline, in both domains.
 *
 * The seconds are what the ends actually are: a beat span occupies different
 * wall-clock seconds depending on where it sits on the tempo curve, so the two
 * ends are converted separately rather than a length being scaled.
 *
 * Two typed ranges rather than four doubles, for the reason TimeDomains.hpp
 * gives: what goes wrong is a value from one domain arriving where the other
 * was meant, and nothing at the call site tells them apart.
 */
struct SnapshotSpan {
    BeatRange beats;
    SecondsRange seconds;
};

/**
 * @brief One audio event of a clip, and how its file is to be read.
 *
 * The source-domain positions stay in samples at the source's own rate, as the
 * model holds them: that is what makes a source-BPM reinterpretation leave the
 * audible region untouched. Converting them is playback's business, and which
 * conversion depends on the interpretation below.
 */
struct AudioEventPlayback {
    EventId eventId = INVALID_EVENT_ID;
    SourceId sourceId = INVALID_SOURCE_ID;

    /// Resolved from the source table at compile time, so nothing on the audio
    /// thread reaches a pool. Always set in a compiled snapshot: an event whose
    /// source the table does not hold is dropped with a diagnostic rather than
    /// carried with nothing to read.
    std::string filePath;
    double sourceSampleRate = 0.0;
    double sourceDurationSeconds = 0.0;

    /// Where this event sits on the timeline, absolute: the clip's start plus
    /// the event's own offset.
    ///
    /// Not cropped to what the lane leaves audible, deliberately. The anchor
    /// below is the sample heard at startSeconds, so this is the origin every
    /// read is derived from; moving it to the audible start would mean
    /// advancing the anchor with it, and by how much is a question only the
    /// stretch and warp settings can answer. The clip's span and its silences
    /// do the cropping, at playback, where that answer exists.
    SnapshotSpan span;

    // ---- Source domain (samples at the source's own rate) ------------------

    std::int64_t anchorSamples = 0;
    std::int64_t loopStartSamples = 0;
    std::int64_t loopLengthSamples = 0;

    /// From the clip: whether the region above repeats to fill the event.
    bool loopEnabled = false;

    // ---- Interpretation ----------------------------------------------------

    double interpBpm = 0.0;
    bool autoTempo = false;
    double speedRatio = 1.0;

    /// What actually runs, not the raw field: a mode left at Off still
    /// stretches when something else forced a stretcher on
    /// (AudioEvent::getEffectiveTimeStretchMode). The values are the pinned
    /// project-file integers in TimeStretchModes.hpp.
    int timeStretchMode = 0;

    /// Resampling rather than stretching, and only in force when nothing else
    /// has already forced a stretcher on (AudioEvent::isAnalogPitchActive).
    bool analogPitch = false;

    /// Whether the event warps at all, which is not the same question as
    /// whether @ref warp bends anything.
    ///
    /// Warp with no markers is identity as a map and is still warp as an
    /// interpretation: the model puts such an event in the beat domain at its
    /// own bpm and forces a stretcher on it
    /// (AudioEvent::sourceInstantToTimelineBeats,
    /// AudioEvent::getEffectiveTimeStretchMode). Reading that off the map being
    /// empty would play it on the seconds face instead, which is a clip that
    /// stops following the tempo the moment its last marker is deleted.
    bool warpEnabled = false;

    /// Compiled and inverted, never the model's marker list (WarpMap.hpp).
    /// Empty is the identity lookup, which is what an event with no markers and
    /// one with none left after the compile both get.
    ///
    /// In the model's own coordinates, forwards. Reverse is not baked in: it is
    /// a coordinate change that EventPlacement.hpp resolves per lookup, because
    /// mirroring the map would need the length of the region the event reads
    /// and that is itself an answer from the map.
    WarpMap warp;

    bool autoPitch = false;
    int autoPitchMode = 0;
    float pitchChange = 0.0f;
    int transpose = 0;

    bool reversed = false;
    bool leftChannelActive = true;
    bool rightChannelActive = true;

    /// This event's own trim. The clip's gain sits above it.
    float gainDb = 0.0f;

    // ---- The event's own fades ---------------------------------------------
    //
    // Its own, and not the clip's. The pair on the clip is what its EDGES play,
    // resolved against the lane; these are what this event fades with inside
    // it. For the event at a clip edge the two are the same fade before and
    // after resolution, so playback shapes that edge once, with the clip's.
    //
    // They are carried per event because fades are per event in the model, and
    // a clip holding several of them (#1901) has fades the clip-level pair
    // cannot express: promoting only the primary event's would lose every
    // other event's.

    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    FadeCurve fadeInCurve = FadeCurve::Linear;
    FadeCurve fadeOutCurve = FadeCurve::Linear;

    /// 0 = gain fade, 1 = speed ramp, as on the clip.
    int fadeInBehaviour = 0;
    int fadeOutBehaviour = 0;
};

/**
 * @brief One audio clip, as it plays.
 *
 * The span is what the lane leaves audible, not the clip's placement, and the
 * silenced ranges are the holes inside it. Nothing on a lane is cut any more, so
 * a clip dropped inside another leaves a hole in the middle of an audio clip
 * rather than splitting it (#2003).
 */
struct AudioClipPlayback {
    ClipId clipId = INVALID_CLIP_ID;

    SnapshotSpan span;
    std::vector<SnapshotSpan> silenced;

    // ---- Fades, already resolved -------------------------------------------
    //
    // What the clip plays with once the lane is known: its own fades, replaced
    // at either edge by a crossfade the length of the overlap that covers it,
    // and scaled to fit when the two would together outrun the clip. A
    // crossfade between two clips is therefore two clips each playing the fade
    // it was given, and nothing downstream pairs them up.

    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    FadeCurve fadeInCurve = FadeCurve::Linear;
    FadeCurve fadeOutCurve = FadeCurve::Linear;

    /// The same two lengths on the beat face, derived here through the same
    /// tempo map. Only a speed ramp asks for them, and only on an auto tempo
    /// clip, where where the material has got to is a question about beats: the
    /// ramp then has to be measured in the domain the position is derived from,
    /// and converting a fade length on the audio thread would need a tempo map
    /// there.
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;

    /// 0 = gain fade, 1 = speed ramp. A speed ramp is a stretch feature rather
    /// than a gain one, which is why it travels with the fade.
    int fadeInBehaviour = 0;
    int fadeOutBehaviour = 0;

    // ---- Mix ---------------------------------------------------------------

    /// The clip's volume and gain summed, which is how the incumbent applies
    /// them, in dB. Per-event trim sits under it on the event.
    float gainDb = 0.0f;
    float pan = 0.0f;

    /// Ramp on the stopped to playing transition. 0 preserves the leading
    /// transient.
    int launchFadeSamples = 0;

    std::vector<AudioEventPlayback> events;
};

/**
 * @brief One MIDI clip, as it plays.
 *
 * The events are the clip's authoritative lists resolved into the messages they
 * play (MidiEventList.hpp), which is what a fronted take or an assembled comp
 * has already been written into: choosing between takes happens in the model,
 * never here, and turning a note into a note-on happens at compile time, never
 * on the audio thread.
 */
struct MidiClipPlayback {
    ClipId clipId = INVALID_CLIP_ID;

    SnapshotSpan span;

    /// A note starting inside one of these does not sound. MIDI plays around a
    /// hole rather than being cut by it.
    std::vector<SnapshotSpan> silenced;

    /// Compiled, never the model's own lists: the curve densification, the MPE
    /// channel assignment, the same-pitch overlap rule and the two offsets all
    /// resolve once, in the compiler that already owns the rest of the
    /// resolution.
    MidiEventList events;

    /// How the clip's content maps onto the timeline: its loop, its phase and
    /// its content origin, which is the only interpretation left for the block
    /// to do.
    MidiFold fold;

    /// Compiled against this clip's own strength, and the one thing here the
    /// audio thread evaluates rather than reads: groove is anchored to the
    /// project grid, so a looped clip grooves each pass differently whenever its
    /// loop length is not a whole multiple of the template's period. Empty is
    /// the identity.
    GrooveTemplate groove;
};

/** Everything one track plays. */
/**
 * @brief One session slot's material, as if it began at beat zero.
 *
 * A session clip has no position on the timeline. Its scene index says which
 * slot it sits in and nothing about when it sounds, because when it sounds is
 * decided by a launch at runtime rather than by the model (#2301). So it is
 * compiled at the origin, describing the material and nothing else, and the
 * launch handle's virtual origin is what maps a block onto it.
 *
 * Compiled through the same path the arrangement's clips go through, so a clip
 * dragged from a slot to the timeline sounds the same in both places.
 */
struct SessionSlotPlayback {
    int sceneIndex = -1;

    /// The slot's material, at most one of each. Empty means the slot holds a
    /// clip that compiled to nothing playable, which is a diagnostic rather
    /// than a slot that does nothing.
    std::vector<AudioClipPlayback> audio;
    std::vector<MidiClipPlayback> midi;

    /// What one pass through the slot is worth, which is what a launch handle
    /// re-triggers on. The clip's own loop is a separate thing that happens
    /// inside this.
    double lengthBeats = 0.0;
};

struct TrackClipPlayback {
    TrackId trackId = INVALID_TRACK_ID;
    std::vector<AudioClipPlayback> audio;
    std::vector<MidiClipPlayback> midi;

    /// Sorted by scene index, so two compiles of one model agree.
    std::vector<SessionSlotPlayback> session;

    /// The slot at @p sceneIndex, or null.
    const SessionSlotPlayback* slot(int sceneIndex) const;
};

/**
 * @brief The whole arrangement, resolved.
 *
 * Tracks are sorted by id and clips within a track by start, so two compiles of
 * the same model produce the same snapshot, which is what makes it golden
 * testable and what keeps a dump diff meaningful.
 */
struct ClipSnapshot {
    static constexpr int kVersion = 1;

    int version = kVersion;

    /// The tempo map the seconds were derived through. A snapshot whose
    /// fingerprint is not the transport's was compiled against a tempo that has
    /// since changed, and every second in it is wrong by however much the map
    /// moved.
    std::uint64_t tempoFingerprint = 0;

    std::vector<TrackClipPlayback> tracks;

    /// Clips the compile could not express, in compile order. Never a silent
    /// drop: a clip that will not sound says here why. A clip the lane leaves
    /// inaudible is not one of these, because being covered is not a failure.
    std::vector<std::string> diagnostics;

    /// The track's playback, or null. A binary search: the audio thread asks
    /// once per block and the list is sorted, so nothing hashes and nothing
    /// allocates.
    const TrackClipPlayback* find(TrackId trackId) const;
};

}  // namespace magda::engine
