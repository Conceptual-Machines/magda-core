#pragma once

#include <string>

#include "clip/ClipSnapshot.hpp"

namespace magda::engine {

/**
 * @brief Render a clip snapshot as canonical text.
 *
 * The snapshot's testable surface, and what to read when what a track plays is
 * not what an edit should have made of it. Deterministic and diff-friendly: one
 * line per clip, per hole and per event, fixed precision, no paths that depend
 * on the machine.
 *
 * Shape, with the long lines cut short here:
 *
 *     magda-clip-snapshot v1
 *     tempo=8f3a1c02 tracks=1
 *     track 1 audio=1 midi=0
 *       audio clip=7 span=0.000..8.000b 0.000..4.000s fade=0.500/0.250 ...
 *         hole 2.000..3.000b 1.000..1.500s
 *         event 1 src=3 file=drums.wav rate=48000 span=... anchor=0 ...
 *     diagnostic: track 1 clip 9: ...
 *
 * The file a source names is dumped as its name only, not its path: a golden
 * test that carried an absolute path would pass on one machine and fail on the
 * next.
 */
std::string dumpClipSnapshot(const ClipSnapshot& snapshot);

}  // namespace magda::engine
