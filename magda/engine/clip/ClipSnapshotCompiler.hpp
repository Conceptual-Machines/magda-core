#pragma once

#include <string>
#include <vector>

#include "clip/ClipSnapshot.hpp"
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
    std::vector<ClipInfo> clips;
};

/**
 * @brief Resolve @p lanes into a snapshot.
 *
 * Deterministic: the same model always compiles to the same snapshot, entry for
 * entry, so a dump diff means a model change and nothing else.
 *
 * Occlusion and fades come from the model's own functions (computeAudibleSpans,
 * effectiveFadesIn) rather than from a second implementation of the rules. The
 * fades are asked for at the tempo where the clip starts, which is the same
 * question the arrangement asks when it draws them: a fade computed any other
 * way here would be a second interpretation, and the one on screen would be the
 * one that was wrong.
 *
 * Anything that will not sound lands in ClipSnapshot::diagnostics rather than
 * disappearing. A clip the lane leaves inaudible is not a diagnostic: being
 * covered is what covering means.
 */
ClipSnapshot compileClipSnapshot(const std::vector<ClipLane>& lanes,
                                 const std::vector<ClipSourceInfo>& sources,
                                 const TempoMap& tempoMap);

}  // namespace magda::engine
