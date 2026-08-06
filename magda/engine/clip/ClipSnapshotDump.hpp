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
 * Shape:
 * @code
 * magda-clip-snapshot v1
 * tempo=8f3a1c02 tracks=1
 * track 1 audio=2 midi=0
 *   audio clip=7 span=0.000..8.000b 0.000..4.000s fade=0.500/0.250 curve=lin/lin gain=-3.0 pan=0.00
 * launch=256 hole 2.000..3.000b 1.000..1.500s event 1 src=3 span=0.000..8.000b anchor=0 loop=off
 * stretch=15 speed=1.000 bpm=120.0
 * @endcode
 *
 * The file a source names is dumped as its name only, not its path: a golden
 * test that carried an absolute path would pass on one machine and fail on the
 * next.
 */
std::string dumpClipSnapshot(const ClipSnapshot& snapshot);

}  // namespace magda::engine
