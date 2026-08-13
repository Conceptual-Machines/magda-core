#pragma once

#include <string>
#include <vector>

#include "core/TrackInfo.hpp"
#include "plan/PlanCompiler.hpp"

namespace magda::goldens {

/**
 * @brief One project the plan compiler is pinned against.
 *
 * A fixture is model values and nothing else. What gets dumped from it, and
 * how, belongs to the runner: a fixture that knew about its own golden file
 * would be a fixture that could be written to make one pass.
 *
 * `deviceLatency` is keyed by where a device sits rather than by op index,
 * because a fixture is written before the plan exists and op indices are the
 * compiler's answer, not the fixture's question. Anything not named here
 * reports zero, so a fixture that is not about latency writes nothing and gets
 * a plan with no delays that hold anything.
 */
struct Fixture {
    /// Names the golden file, so it has to be unique and stable. Renaming one
    /// orphans its file, which the runner reports rather than ignores.
    std::string name;

    /// What the fixture is for, printed into the golden so the file explains
    /// itself to whoever a failing diff lands on.
    std::string covers;

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    engine::CompileOptions options;

    /// Device latencies in samples, keyed by the op the latency belongs to.
    ///
    /// The whole key rather than the device id alone, because a device id does
    /// not identify a device: TrackManager allocates them per section, so one
    /// track's FX and post-FX chains can both hold id 3. Op keys do not carry
    /// a section either, which is its own problem and not this file's, but a
    /// fixture that states where it means is at least not adding to it.
    ///
    /// The runner requires each entry to match exactly one Device op, which is
    /// what turns a mistyped location into a failure instead of a golden
    /// quietly pinned with no delays in it.
    std::vector<std::pair<engine::OpKey, int>> deviceLatency;

    /// A second project, compiled and diffed against the first. Empty where
    /// the fixture is not about what survives an edit.
    std::vector<TrackInfo> editedTracks;
};

/** Every fixture, in the order their goldens are written. */
std::vector<Fixture> planFixtures();

}  // namespace magda::goldens
