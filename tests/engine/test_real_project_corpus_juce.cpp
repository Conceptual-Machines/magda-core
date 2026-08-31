#include <juce_core/juce_core.h>

#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "MgdFixture.hpp"
#include "NullDiffCompare.hpp"
#include "NullDiffRunner.hpp"

/**
 * The real-project corpus (#2081): projects somebody saved, through both
 * engines.
 *
 * The code-built corpus (#2040) tests the rules whoever wrote it knew about. It
 * is twenty-odd cases of clip model values, each one built to isolate a
 * question, and every one of them is a project nobody would ever make. That is
 * a virtue where it is aimed -- a case that isolates trims tells you about
 * trims -- and it is also its ceiling: a rule nobody wrote down is a rule
 * nothing in it exercises.
 *
 * This is the other half. Projects saved by released builds between 0.4.8 and
 * 0.17.0, loaded from the bytes they were written as, rendered through both
 * engines and compared by the same runner the code-built corpus uses. Seven are
 * declared; six run here, and the skip list below says why the seventh does
 * not. What they
 * bring is the combinations: a v1 project whose clips carry their sources
 * inline, a five-device chain under an FM synth, two tracks coupled through a
 * sidechain rather than through routing, sixty-four session clips sitting
 * beside an arrangement, a warp map made by a real transient pass with a
 * repeated marker in it.
 *
 * MgdFixtures.cpp says which projects and why, and which fifteen of the
 * twenty-two are not here.
 *
 * ## Where these run
 *
 * #2081 left this open deliberately: "whether these run in the ordinary test
 * job or a longer one is decided when their cost is known", because a gate
 * budgeted by guesswork is a gate somebody disables.
 *
 * Measured: about twenty seconds, both legs and the comparison, on a debug
 * build. Half of it is the demo project alone -- eight tracks, four
 * instruments, a master limiter and a stretched clip, ten seconds of it -- and
 * the rest are a second or three each. The clip corpus beside it is budgeted at
 * thirty, so this roughly doubles what the null-diff suite costs and it stays
 * in the ordinary job.
 *
 * The figure is printed on every run rather than recorded here, so the day it
 * stops being true the run says so instead of this comment lying quietly. What
 * would make it stop is a project longer than sixteen beats: #2081 raised a
 * forty-minute arrangement, and one of those rendered twice is not thirty
 * seconds however the rest of the corpus is budgeted. The window each fixture
 * renders is a manifest field for that reason (MgdFixtures.cpp) -- a project
 * earns its place by what it contains, not by how much of it is played.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

/// The scratch root the fixtures write their stand-in material into.
///
/// Under the corpus's own directory rather than beside it: the two corpora
/// write material with the same names for different reasons, and a fixture
/// whose guitar loop was overwritten by a code-built case's would render the
/// wrong sound and null against the incumbent doing the same.
juce::File fixtureScratch() {
    auto root = nullDiffScratchDirectory().getChildFile("projects");
    root.createDirectory();
    return root;
}

}  // namespace

class RealProjectCorpusTests : public juce::UnitTest {
  public:
    RealProjectCorpusTests() : juce::UnitTest("Real Project Corpus", "magda") {}

    void runTest() override {
        beginTest("Every real project, through both engines");

        // Held for the whole test. The load drives the app's own source
        // install, which clears the pool: right for an app with one project
        // open, and fatal in a binary that shares the pool with the code-built
        // corpus. See PooledSourcesUnwind.
        const PooledSourcesUnwind unwind;

        // Fixtures this binary cannot measure, named rather than filtered by a
        // predicate, so the reason travels with the name and the day it stops
        // being true somebody has to come here to say so.
        //
        // project.faust hosts a runtime-compiled Faust device, and the Faust
        // standard library is not staged beside this binary: staging it aborts
        // the suite on Linux, in an unrelated test that instantiates the Faust
        // instrument, inside libfaust's own interval analysis (#2238).
        // tests/CMakeLists.txt has the mechanism, and why the patch fix that
        // looked like it would clear this one did not. Without the library the device's saved
        // source will not compile and it passes audio through, so both legs would render two chains
        // that are not running the device the project named and null against each other perfectly
        // -- a case that passes by testing less than it claims, which is the one outcome this
        // corpus exists to refuse.
        //
        // It is not lost coverage: magda_tests does stage the library and
        // renders this project through the block-size gate, where it is one of
        // the four that found a dependence.
        const std::set<std::string> unmeasurableHere{"project.faust"};

        std::vector<Case> cases;
        std::vector<std::string> refusals;
        std::set<std::string> skipped;

        for (const auto& fixture : mgdFixtures()) {
            if (unmeasurableHere.count(fixture.declaration.name) != 0) {
                skipped.insert(fixture.declaration.name);
                continue;
            }

            const auto loaded = loadFixture(fixture, fixtureScratch());

            // A fixture that will not load is a failure of the corpus rather
            // than of the engine, and it is reported as one: it never reaches a
            // leg, so there is no residual to attribute it to and nothing on the
            // calibration list could forgive it.
            if (!loaded.ok) {
                refusals.push_back(std::string(fixture.declaration.name) + ": " + loaded.failure);
                continue;
            }

            cases.push_back(loaded.value);
        }

        expect(refusals.empty(), "fixtures that would not load: " + join(refusals));

        // Both directions, the same rule the calibration list lives by: a name
        // on the list that is no longer a fixture has to come off it.
        expect(skipped.size() == unmeasurableHere.size(),
               "the skip list names a fixture the corpus does not carry");

        const auto run = runSuite(cases, [this](const juce::String& line) { logMessage(line); });

        // Printed on every run rather than only on failure, the same as the
        // code-built corpus: a number that moves is what an engine going subtly
        // wrong looks like before it goes audibly wrong.
        const auto report = formatReport(run.reports, cases.empty() ? CaseEnvironment{}
                                                                    : cases.front().environment());
        logMessage(report);
        std::cout << report << std::endl;

        logMessage(costOf(cases, run));
        std::cout << costOf(cases, run) << std::endl;

        // What the real-project corpus asserts today, and what it is still
        // calibrating.
        //
        // The same list the code-built corpus keeps and under the same rule: it
        // forgives a comparison that has a number but not yet a bound with a
        // mechanism behind it, and it forgives nothing else. Membership is
        // asserted in both directions, so a project that starts failing has to
        // be added by somebody with a reason and one that starts holding has to
        // be taken out.
        //
        // Where each one stands is written beside it below, from the run that
        // produced these numbers.
        const std::set<std::string> underCalibration{
            // The stretched project. It is in the spectral tier for a declared
            // mechanism -- a loop stretched from 82.55 to 120 -- and has no
            // predicted shift yet, which the tier refuses rather than measures.
            // Predicting one means reading what the engine primes its stretcher
            // with on this project and checking it against what the fork's copy
            // of the same library does, which is the calibration the code-built
            // stretch cases went through and is a measurement rather than a
            // guess.
            //
            // Its numbers moved when the master chain started rendering
            // (#2175): this project carries a limiter there, and until then the
            // corpus was comparing the incumbent's limited render against an
            // unlimited native one. The envelopes correlate at 0.417 now where
            // they correlated at 0.442 before, which is the comparison getting
            // honest rather than worse -- and whoever calibrates the shift
            // should expect to find two mechanisms in here rather than one, the
            // stretch priming and whatever the two limiters do differently.
            //
            // project.faust is the third, and it is skipped in this binary
            // rather than calibrated here; see the list above.
            "project.demo",

            // The two device projects, and the reason they are here is not that
            // their residual is unexplained. It is that the native leg does not
            // yet have one render to compare.
            //
            // Both fail the block-size gate (#2078, test_null_diff_block_size.cpp):
            // fmchain differs by 5 dB between 512 and every other size,
            // sidechain by 7 to 9 dB. RenderContext's claim is that output is a
            // function of timeline position, so until that holds there is no
            // single native render for the incumbent to be compared against,
            // and whatever this file measures at 512 is one of several answers.
            // Pinning a bound on it would be pinning a bound to a block size.
            //
            // What they measure today, for whoever picks the block-size
            // question up: fmchain is 5.4 dB peak and 21.9 dB rms from the
            // incumbent with the two correlating at 0.12 at any offset, so the
            // two renders are different content rather than the same content
            // moved; sidechain is 3.6 dB peak and 16.5 dB rms with the two
            // correlating at 0.83, the same sound with a different ducking
            // envelope -- the native leg is consistently the louder of the two
            // through every decay.
            "project.fmchain",
            "project.sidechain",
        };

        const auto complaints =
            judgeSuite(run.asserted, run.unmeasurable, run.failing, underCalibration);

        // Every fixture reached a verdict. Asserted positively rather than left
        // implied, because the checks below all pass on an empty run and a
        // corpus that rendered nothing would look exactly like one that agreed
        // about everything.
        expect(run.reports.size() == cases.size(),
               "expected " + juce::String(static_cast<int>(cases.size())) + " reports, got " +
                   juce::String(static_cast<int>(run.reports.size())));
        expect(!cases.empty(), "the real-project corpus rendered nothing");

        expect(complaints.asserted.empty(),
               "asserted while rendering, which is never a result the calibration list forgives: " +
                   join(complaints.asserted));

        expect(
            complaints.unmeasurable.empty(),
            "could not be measured at all, which the calibration list does not forgive either: " +
                join(complaints.unmeasurable));

        expect(complaints.unexpectedFailures.empty(),
               "did not hold: " + join(complaints.unexpectedFailures));

        expect(complaints.nowHolding.empty(),
               "now holds and should come off the calibration list: " +
                   join(complaints.nowHolding));
    }

  private:
    static juce::String join(const std::vector<std::string>& names) {
        juce::StringArray parts;
        for (const auto& name : names)
            parts.add(juce::String(name));
        return parts.joinIntoString(", ");
    }

    /**
     * @brief What this corpus costs, per project and in total.
     *
     * The open question on #2081, answered by measuring rather than by deciding
     * in advance. Both legs and the comparison, wall clock, beside the beats
     * each case rendered so a total can be read against a project that is
     * longer rather than merely slower.
     */
    static std::string costOf(const std::vector<Case>& cases, const SuiteRun& run) {
        juce::String text;
        text << "\nmagda-real-project-cost\n";
        text << "  project                     beats    ms\n";

        for (std::size_t index = 0; index < cases.size() && index < run.milliseconds.size();
             ++index) {
            const auto& value = cases[index];
            text << "  " << juce::String(value.name).paddedRight(' ', 26) << " "
                 << juce::String(value.endBeat - value.startBeat, 1).paddedLeft(' ', 6) << " "
                 << juce::String(static_cast<int>(run.milliseconds[index])).paddedLeft(' ', 6)
                 << "\n";
        }

        const auto total =
            std::accumulate(run.milliseconds.begin(), run.milliseconds.end(), std::int64_t{0});
        text << "  total " << juce::String(static_cast<int>(total)) << " ms across "
             << juce::String(static_cast<int>(cases.size())) << " projects\n";

        return text.toStdString();
    }
};

namespace {
RealProjectCorpusTests realProjectCorpusTests;
}  // namespace
