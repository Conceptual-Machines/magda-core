#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "PlanGoldenFixtures.hpp"
#include "TextDifference.hpp"
#include "exec/PlanLayoutDump.hpp"
#include "param/ParamTableCompiler.hpp"
#include "param/ParamTableDump.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanCrossfade.hpp"
#include "plan/PlanDiff.hpp"
#include "plan/PlanDiffDump.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_plan_goldens.cpp
 * @brief The plan compiler's decisions, pinned as checked-in text (#2076).
 *
 * A render says the output is wrong. A golden says which of the compiler's
 * decisions changed, before any audio exists, and a diff of two canonical
 * dumps is readable in a way a waveform is not.
 *
 * ## Regenerating
 *
 * When a change to the plan is intended:
 *
 *     MAGDA_UPDATE_PLAN_GOLDENS=1 ./magda_tests "[goldens]"
 *
 * That rewrites every file under tests/goldens/plan and passes. The diff is
 * then the reviewable artefact, which is the entire point: a golden that is
 * regenerated without being read has stopped being a test, so the rewrite is
 * deliberately not something a normal run can do.
 *
 * ## What is in a golden, and what is kept out
 *
 * Only decisions. No addresses, no timings, no allocation or iteration order,
 * and nothing keyed on an op index across plans: the diff section prints op
 * keys, because inserting one device ahead of another moves every index behind
 * it and a golden written in indices would churn on an edit that carried
 * correctly.
 *
 * Stability is pinned rather than assumed. Every fixture is compiled twice in
 * this process and the two dumps compared, and the files themselves are the
 * cross-process half of the same check: each was written by a different run of
 * a different build than the one asserting it now.
 */

using namespace magda;
using magda::engine::RenderPlan;

namespace {

const char* const kGoldenDir = MAGDA_PLAN_GOLDEN_DIR;

bool regenerating() {
    const auto* flag = std::getenv("MAGDA_UPDATE_PLAN_GOLDENS");
    return flag != nullptr && *flag != '\0' && std::string(flag) != "0";
}

juce::File goldenFile(const std::string& name) {
    return juce::File(kGoldenDir).getChildFile(juce::String(name) + ".txt");
}

/// Device latency per op, which is what the layout pass takes.
///
/// Each entry has to match exactly one op. A fixture names a device by where
/// it sits, and a location that matches nothing is a fixture describing a
/// project it did not build: it would compile fine, print a golden with no
/// delays in it, and pin the wrong graph forever.
std::vector<int> deviceLatencyPerOp(const RenderPlan& plan,
                                    const std::vector<std::pair<engine::OpKey, int>>& byLocation) {
    std::vector<int> latency(plan.ops.size(), 0);

    for (const auto& [key, samples] : byLocation) {
        int matches = 0;

        for (std::size_t i = 0; i < plan.ops.size(); ++i) {
            if (plan.ops[i].kind != engine::OpKind::Device || plan.ops[i].key != key)
                continue;

            latency[i] = samples;
            ++matches;
        }

        INFO("device latency for " << engine::toString(key));
        REQUIRE(matches == 1);
    }

    return latency;
}

/// Everything a fixture pins, as one document.
///
/// One file per fixture rather than one per section: a change that moves an op
/// moves it in the plan, the layout and the diff at once, and three files
/// would make one decision look like three regressions.
std::string renderGolden(const goldens::Fixture& fixture) {
    std::ostringstream out;
    out << "# " << fixture.name << "\n";
    out << "# " << fixture.covers << "\n";
    out << "#\n";
    out << "# Generated. See tests/test_plan_goldens.cpp to regenerate.\n\n";

    const auto plan = engine::compileRenderPlan(fixture.tracks, fixture.master, fixture.options);

    out << "== plan ==\n" << engine::dumpPlan(plan) << "\n";
    out << "== layout ==\n"
        << engine::dumpPlanLayout(plan, deviceLatencyPerOp(plan, fixture.deviceLatency)) << "\n";

    // The parameters resolved against the same plan (#2117). Here rather than
    // in a file of their own for the reason the layout is: a model change moves
    // the graph and the parameters at once, and two files would make one
    // decision look like two regressions.
    out << "== params ==\n"
        << engine::dumpParamTable(engine::compileParamTable(plan, fixture.tracks, fixture.master,
                                                            fixture.lanes, fixture.clips))
        << "\n";

    if (fixture.editedTracks.empty())
        return out.str();

    const auto edited =
        engine::compileRenderPlan(fixture.editedTracks, fixture.master, fixture.options);

    out << "== edited plan ==\n" << engine::dumpPlan(edited) << "\n";
    out << "== diff ==\n"
        << engine::dumpPlanDiff(plan, edited, engine::diffPlans(plan, edited)) << "\n";

    const auto faded = engine::insertCrossfades(plan, edited);
    out << "== crossfaded ==\n"
        << "inserted=" << faded.inserted << " unfaded=" << faded.unfaded << "\n"
        << engine::dumpPlan(faded.plan);

    return out.str();
}

/// Exactly one newline at the end, because the repo's end-of-file hook would
/// otherwise rewrite the goldens the first time one was committed and every
/// run after that would compare against a file it did not write.
std::string oneTrailingNewline(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    return text + "\n";
}

}  // namespace

TEST_CASE("Plan goldens hold", "[engine][plan][goldens]") {
    const auto fixtures = goldens::planFixtures();
    REQUIRE_FALSE(fixtures.empty());

    for (const auto& fixture : fixtures) {
        const auto file = goldenFile(fixture.name);
        const auto actual = oneTrailingNewline(renderGolden(fixture));

        if (regenerating()) {
            file.getParentDirectory().createDirectory();

            // Line endings named rather than defaulted: replaceWithText writes
            // CRLF unless told otherwise, and a golden regenerated on one
            // platform has to be the file every other platform asserts.
            REQUIRE(file.replaceWithText(juce::String(actual), false, false, "\n"));
            continue;
        }

        INFO("fixture: " << fixture.name);
        INFO("golden: " << file.getFullPathName());

        if (!file.existsAsFile()) {
            FAIL_CHECK("no golden: regenerate with MAGDA_UPDATE_PLAN_GOLDENS=1");
            continue;
        }

        const auto expected = file.loadFileAsString().toStdString();
        const auto difference = firstDifference(expected, actual);

        INFO(difference);
        CHECK(difference.empty());
    }
}

TEST_CASE("Every golden on disk belongs to a fixture", "[engine][plan][goldens]") {
    // A renamed or deleted fixture leaves its file behind, and a stale golden
    // is a file nothing asserts that still looks like coverage.
    std::set<std::string> expected;
    for (const auto& fixture : goldens::planFixtures())
        expected.insert(fixture.name + ".txt");

    for (const auto& file :
         juce::File(kGoldenDir).findChildFiles(juce::File::findFiles, false, "*.txt")) {
        const auto name = file.getFileName().toStdString();
        INFO("orphan golden: " << name);
        CHECK(expected.count(name) == 1);
    }
}

TEST_CASE("A fixture compiles to the same text twice in one process",
          "[engine][plan][goldens][determinism]") {
    // The other half of the stability the goldens depend on. The files pin the
    // cross-process case by existing; this pins the case a single run could
    // still get wrong, where a dump reads back allocation or iteration order.
    for (const auto& fixture : goldens::planFixtures()) {
        INFO("fixture: " << fixture.name);
        CHECK(oneTrailingNewline(renderGolden(fixture)) ==
              oneTrailingNewline(renderGolden(fixture)));
    }
}

TEST_CASE("Fixture names are unique", "[engine][plan][goldens]") {
    // Two fixtures with one name means one golden file, silently written twice
    // and asserted against whichever ran last.
    std::set<std::string> seen;
    for (const auto& fixture : goldens::planFixtures()) {
        INFO("fixture: " << fixture.name);
        CHECK(seen.insert(fixture.name).second);
    }
}
