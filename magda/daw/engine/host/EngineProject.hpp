#pragma once

#include <vector>

#include "clip/ClipSnapshotCompiler.hpp"
#include "core/TrackInfo.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file EngineProject.hpp
 * @brief The project as magda::engine wants it, read off the app's model.
 *
 * The engine compiles from plain values and reaches no singleton, which is what
 * makes a compile deterministic (ClipSnapshotCompiler.hpp). Somebody still has
 * to go to ClipManager and SourcePool for those values, and this is that
 * somebody, so the host reads as a publish rather than as a tour of the model.
 *
 * Tracks are not here: TrackManager already holds them in the shape the
 * compiler takes, so the host passes them straight through.
 */

namespace magda::daw::engine_host {

/// What each of @p tracks plays, arrangement and session apart. The split is
/// the caller's to make: a session clip is a slot positioned by scene, and the
/// compiler's guard exists to catch a caller who confused the two.
std::vector<engine::ClipLane> clipLanesFor(const std::vector<TrackInfo>& tracks);

/// Every pooled source, as facts. The engine never probes a file: a snapshot
/// compiled during playback would open one on the publishing thread.
std::vector<engine::ClipSourceInfo> clipSources();

/// A flat map at @p bpm. Flat because the model is: tempo curves live in the
/// fork's tempo sequence and the app has no tempo track of its own to bake
/// (#2554 moves that).
engine::TempoMap tempoMapAt(double bpm, int numerator, int denominator);

/// Where the last arrangement clip ends, in beats, or zero when there are none.
/// What the length of a project is with no Edit to ask (#2579).
double projectEndBeat();

/**
 * @brief Let a pooled source say what tempo its file was written at (#2552).
 *
 * Nothing filled Source::detectedBpm before this. A clip's tempo came from
 * Tracktion's loopInfo through ClipSynchronizer, so with no Edit an imported
 * loop had no tempo, could not be in beat mode, and looped at whatever length
 * its seconds happened to be. The parse is the engine's
 * (magda/engine/io/SourceLoopInfo.hpp) and is installed from here because this
 * side sees both headers.
 *
 * Once, at startup. Only fills what the file did not already say, like every
 * other seeding rule in the model.
 */
void installSourceTempoProbe();

}  // namespace magda::daw::engine_host
