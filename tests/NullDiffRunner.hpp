#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "NullDiffCase.hpp"
#include "NullDiffCompare.hpp"

/**
 * @file NullDiffRunner.hpp
 * @brief One case through both engines, and what a suite of them is held to.
 *
 * Lifted out of the corpus test rather than written for the second corpus
 * (#2081). Two corpora now render cases: the code-built one from #2040 and the
 * real projects loaded by the fixture rig, and they ask the same question of
 * two engines. A second copy of the judgement is the thing this whole harness
 * exists to refuse -- it can agree with itself while both are wrong, and here
 * it would do so about which residual counts as a null.
 *
 * So the tiers, the two silences guard, the diagnostics contract, the MIDI
 * split by track and the alignment all live here once, and a corpus is a list
 * of cases plus what it forgives.
 */

namespace magda::nulldiff {

/// Where a runner says what it found. A UnitTest passes its own logMessage; a
/// caller that only wants the numbers passes something that drops them.
using RunnerLog = std::function<void(const juce::String&)>;

/// Where a failing case's artefacts go, and where the corpus writes material.
juce::File nullDiffScratchDirectory();

/**
 * @brief Render @p value through both engines and say what the comparison
 *        found.
 *
 * Never throws a verdict away: a leg that would not render, a diagnostic the
 * case did not declare, a read-back that was not float and a length mismatch
 * all come back as @c unmeasurable rather than as a residual, because a race
 * reported as a parity failure costs somebody a day inside the engine looking
 * for it.
 */
CaseReport runCase(const Case& value, const RunnerLog& log);

/**
 * @brief What the suite has to complain about, given a run.
 *
 * Separated from the runner so the rule can be tested without rendering
 * anything, because one part of it is easy to get wrong in a way nothing would
 * notice.
 *
 * A case under calibration is *expected* to fail its comparison, and the
 * membership check is asserted in both directions so that neither a new failure
 * nor a fixed one can happen quietly.
 *
 * What the list forgives is precisely one thing: a comparison result that has a
 * number but not yet a bound with a mechanism behind it. It does not forgive not
 * having measured. Two outcomes therefore sit outside it entirely, because both
 * would otherwise meet the expected-failure membership by producing no
 * comparison at all:
 *
 *  - **An assertion.** The engine objecting to its own graph is not a residual,
 *    and a calibrating case that asserted would look exactly like one that
 *    failed the way it was forgiven for.
 *  - **Anything unmeasurable.** A leg that would not render, a proxy that never
 *    arrived, a fixed-point read-back, a comparator refusal, a length mismatch.
 *    Every one of them leaves `passed` false with nothing measured, and a
 *    calibrating case would swallow it.
 */
struct SuiteComplaints {
    /// Asserted while rendering. Any case, calibrating or not.
    std::vector<std::string> asserted;

    /// Produced no comparison at all. Any case, calibrating or not. Excludes
    /// the ones that asserted, which are the same failure named better.
    std::vector<std::string> unmeasurable;

    /// Failed its comparison and was not expected to.
    std::vector<std::string> unexpectedFailures;

    /// Expected to fail and no longer does, so it should come off the list.
    std::vector<std::string> nowHolding;

    bool empty() const {
        return asserted.empty() && unmeasurable.empty() && unexpectedFailures.empty() &&
               nowHolding.empty();
    }
};

SuiteComplaints judgeSuite(const std::set<std::string>& asserted,
                           const std::set<std::string>& unmeasurable,
                           const std::set<std::string>& failing,
                           const std::set<std::string>& underCalibration);

/// One walk over a corpus, with everything a suite needs to judge itself.
struct SuiteRun {
    std::vector<CaseReport> reports;

    /// Read per case, so an assertion is attributed to the case that provoked
    /// it rather than to the run.
    std::set<std::string> asserted;
    std::set<std::string> failing;
    std::set<std::string> unmeasurable;

    /// Never rendered, because the project names a plugin this machine has not
    /// scanned (#2175).
    ///
    /// Kept apart from the three above because it is not a complaint. Those are
    /// things wrong with the engine or with the harness; this is a fact about
    /// which plugins are installed, and every CI runner has none. A suite reads
    /// it to print what it did not cover, not to fail.
    std::set<std::string> notRun;

    /// Wall clock per case, in milliseconds, both legs and the comparison.
    ///
    /// Recorded on every run rather than under a flag, because #2081's open
    /// question is where a corpus of real projects can afford to run, and that
    /// is answered by reading the number a normal run already printed rather
    /// than by anybody remembering to measure.
    std::vector<std::int64_t> milliseconds;
};

/**
 * @brief Run @p cases in order, reading the assertion watch between them.
 *
 * The watch is taken once before the walk begins, to drop anything logged on
 * the way in: that belongs to whatever ran before rather than to the first
 * case.
 */
SuiteRun runSuite(const std::vector<Case>& cases, const RunnerLog& log);

}  // namespace magda::nulldiff
