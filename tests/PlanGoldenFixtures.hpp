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
 * `deviceLatency` is per device id rather than per op, because a fixture is
 * written before the plan exists and op indices are the compiler's answer, not
 * the fixture's question. Anything not named here reports zero, so a fixture
 * that is not about latency writes nothing and gets a plan with no delays that
 * hold anything.
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

    /// Device latencies in samples, as `{deviceId, samples}`.
    std::vector<std::pair<DeviceId, int>> deviceLatency;

    /// A second project, compiled and diffed against the first. Empty where
    /// the fixture is not about what survives an edit.
    std::vector<TrackInfo> editedTracks;
};

/** Every fixture, in the order their goldens are written. */
std::vector<Fixture> planFixtures();

}  // namespace magda::goldens
