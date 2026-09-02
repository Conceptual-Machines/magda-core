#pragma once

#include <string>
#include <vector>

#include "clip/ClipSnapshot.hpp"
#include "clip/GrooveTemplate.hpp"
#include "core/ClipInfo.hpp"
#include "core/TypeIds.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file ClipSnapshotCompiler.hpp
 * @brief Compile the clip model into what the audio thread plays.
 *
 * Data in, data out: the caller hands over the lanes as the model holds them
 * and the facts about the files they name, and gets back a resolved snapshot.
 * Nothing is looked up behind the caller's back, which is what makes the
 * compile deterministic and golden testable, and what keeps the engine off the
 * message-thread singletons (ClipManager, SourcePool) that own this data in the
 * app.
 */

namespace magda::engine {

/**
 * @brief What the engine needs to know about one media source.
 *
 * The facts about the file, resolved by whoever owns the pool. The engine never
 * probes a file: a snapshot compiled during playback would otherwise open one.
 */
struct ClipSourceInfo {
    SourceId id = INVALID_SOURCE_ID;
    std::string path;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
};

/**
 * @brief One track's arrangement clips, as the model holds them.
 *
 * Every arrangement clip on the track, in any order, including the ones that
 * are covered or disabled: occlusion is resolved per lane and a clip that is
 * missing from it changes what the others play.
 */
struct ClipLane {
    TrackId trackId = INVALID_TRACK_ID;

    /// The arrangement's clips, positioned on the timeline. A session clip here
    /// is a diagnostic rather than a clip: the two are different questions and
    /// the caller is the one that knows which it is asking.
    std::vector<ClipInfo> clips;

    /// The track's session slots, positioned by scene rather than by beat.
    /// Separate because they are not alternatives at the same place: a track
    /// can carry an arrangement and a session at once, and which one sounds is
    /// decided at launch rather than at compile (#2301).
    std::vector<ClipInfo> session;
};

/**
 * @brief Resolve @p lanes into a snapshot.
 *
 * Deterministic: the same model always compiles to the same snapshot, entry for
 * entry, so a dump diff means a model change and nothing else. Tracks and the
 * clips on them are sorted here, but diagnostics keep the order the lanes
 * arrived in, so a caller that wants two compiles to dump alike hands the lanes
 * over in a stable order. Project order is the obvious one.
 *
 * Occlusion and fades come from the model's own functions (computeAudibleSpans,
 * effectiveFadesIn) rather than from a second implementation of the rules. What
 * this adds is the tempo map: which edge a crossfade covers is the model's
 * answer, how long those beats last is asked of the map, so the two halves of
 * one crossfade agree even when their clips start under different tempos. The
 * arrangement still draws its curves at the project tempo, which is the same
 * number on a flat map and a thing to reconcile when tempo curves reach
 * playback.
 *
 * Anything that will not sound lands in ClipSnapshot::diagnostics rather than
 * disappearing, and does not reach the snapshot: a clip with nothing playable
 * left is not carried as an empty one. A clip the lane leaves inaudible is
 * neither, because being covered is what covering means.
 */
/// @overload
/// @param grooves The named groove templates a MIDI clip may ask for, as
///        whoever owns them holds them. Handed over rather than looked up, like
///        the sources and the tempo map: the engine reaches no singleton, and a
///        compile is a pure function of what it was given. An empty set is legal
///        and means no clip grooves, which is what the engine gets until the app
///        is switched over to it.
ClipSnapshot compileClipSnapshot(const std::vector<ClipLane>& lanes,
                                 const std::vector<ClipSourceInfo>& sources,
                                 const TempoMap& tempoMap, const GrooveTemplateSet& grooves);

inline ClipSnapshot compileClipSnapshot(const std::vector<ClipLane>& lanes,
                                        const std::vector<ClipSourceInfo>& sources,
                                        const TempoMap& tempoMap) {
    static const GrooveTemplateSet none;
    return compileClipSnapshot(lanes, sources, tempoMap, none);
}

}  // namespace magda::engine
