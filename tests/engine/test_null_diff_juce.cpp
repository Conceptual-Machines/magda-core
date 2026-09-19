#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>

#include "NullDiffCompare.hpp"
#include "NullDiffNativeLeg.hpp"
#include "NullDiffRunner.hpp"
#include "NullDiffTeLeg.hpp"
#include "SharedTestEngine.hpp"
#include "clip/WarpMap.hpp"
#include "transport/TempoMap.hpp"

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

#include "AssertionWatch.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"

/**
 * The null-diff corpus (#2040): every case rendered through both engines.
 *
 * This is what decides the engine is right rather than merely tested. Six
 * slices were judged by tests written beside them, and a test asserts what its
 * author believed the rule was; where an author misread the incumbent, the test
 * agrees with the misreading. A project rendered through both engines cannot,
 * because neither engine gets a say in what the other produces.
 *
 * It runs here rather than in the Catch2 target because the incumbent leg is a
 * te::Edit, and Edits in that target take later tests down with them.
 *
 * Read the two legs before this file. Most of what makes a corpus honest is in
 * them: material chosen so that a residual can only be a bug, a render kept in
 * float, and a proxy waited for rather than raced.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace te = tracktion;

// =============================================================================
// The two maps, compared as functions
// =============================================================================

class NullDiffMapTests : public juce::UnitTest {
  public:
    NullDiffMapTests() : juce::UnitTest("Null Diff Maps", "magda") {}

    void runTest() override {
        testTempoMaps();
        testWarpMaps();
    }

  private:
    /// Both are position mappings, both move every sample of everything
    /// downstream of them, and both are exactly diffable. Comparing them through
    /// audio would take an answer that is a number and turn it into a waveform.
    void testTempoMaps() {
        beginTest("Tempo maps agree as functions");

        auto& wrapper = magda::test::getSharedEngine();
        auto* engine = wrapper.getEngine();
        expect(engine != nullptr, "engine");
        if (engine == nullptr)
            return;

        struct Shape {
            const char* name;
            std::vector<TempoPoint> points;
        };

        for (const auto& shape :
             {Shape{"one tempo", {{0.0, 120.0}}}, Shape{"a step up", {{0.0, 120.0}, {8.0, 140.0}}},
              Shape{"a step down", {{0.0, 174.0}, {4.0, 96.0}}},
              Shape{"three steps", {{0.0, 90.0}, {6.0, 128.0}, {13.0, 101.0}}}}) {
            auto edit = te::test_utilities::createTestEdit(*engine, 1);
            if (edit == nullptr)
                continue;

            auto& sequence = edit->tempoSequence;
            while (sequence.getNumTempos() > 1)
                sequence.removeTempo(sequence.getNumTempos() - 1, false);
            if (auto* first = sequence.getTempo(0))
                first->setBpm(shape.points.front().bpm);
            for (std::size_t index = 1; index < shape.points.size(); ++index)
                sequence.insertTempo(te::BeatPosition::fromBeats(shape.points[index].beat),
                                     shape.points[index].bpm, 1.0f);

            std::vector<engine::TempoChange> changes;
            for (const auto& point : shape.points) {
                if (!changes.empty())
                    changes.push_back({point.beat, changes.back().bpm, 0.0f});
                changes.push_back({point.beat, point.bpm, 0.0f});
            }
            const engine::TempoMap map(std::move(changes), {});

            auto worst = 0.0;
            auto worstBeat = 0.0;
            for (auto beat = 0.0; beat <= 32.0; beat += 0.05) {
                const auto mine = map.beatToTime(beat);
                const auto theirs = sequence.toTime(te::BeatPosition::fromBeats(beat)).inSeconds();
                if (std::abs(mine - theirs) > worst) {
                    worst = std::abs(mine - theirs);
                    worstBeat = beat;
                }
            }

            // A beat that lands at a different second moves every clip in the
            // project, so this is asserted to the microsecond and failed here,
            // once, rather than twenty-one times as a waveform.
            expect(worst < 1.0e-6, juce::String(shape.name) + ": worst disagreement " +
                                       juce::String(worst, 9) + " s at beat " +
                                       juce::String(worstBeat, 3));
        }
    }

    /// Where warp coverage actually lives. A warped clip forces a stretcher on,
    /// so an audio case would be two different stretch pipelines with a priming
    /// shift on top, which is exactly what the corpus's material rule refuses.
    /// The map is where the semantics are, and it is exact.
    void testWarpMaps() {
        beginTest("Warp maps agree as functions");

        auto& wrapper = magda::test::getSharedEngine();
        auto* engine = wrapper.getEngine();
        expect(engine != nullptr, "engine");
        if (engine == nullptr)
            return;

        struct Shape {
            const char* name;
            std::vector<WarpMarker> markers;
        };

        for (const auto& shape :
             {Shape{"one segment", {{0.0, 0.0}, {2.0, 3.0}}},
              Shape{"three segments", {{0.0, 0.0}, {1.0, 1.5}, {3.0, 3.0}, {5.0, 6.5}}},
              Shape{"markers off the origin", {{1.0, 2.0}, {4.0, 4.5}}}}) {
            auto edit = te::test_utilities::createTestEdit(*engine, 1);
            if (edit == nullptr)
                continue;

            const auto compiled = engine::compileWarpMap(shape.markers);
            const auto& map = compiled.map;

            auto worst = 0.0;
            auto worstAt = 0.0;

            // Across the marker range and well past it, because slope 1 outside
            // is as much a part of the map as the segments inside are.
            for (auto warp = -1.0; warp <= 9.0; warp += 0.01) {
                const auto mine = map.sourceSecondsAt(warp);

                // The incumbent's own answer, from the same markers. Built as a
                // piecewise-linear map the same way, so a disagreement is about
                // the mapping rather than about who owns the markers.
                const auto theirs = incumbentSourceSeconds(shape.markers, warp);

                if (std::abs(mine - theirs) > worst) {
                    worst = std::abs(mine - theirs);
                    worstAt = warp;
                }
            }

            expect(worst < 1.0e-6, juce::String(shape.name) + ": worst disagreement " +
                                       juce::String(worst, 9) + " s at warp time " +
                                       juce::String(worstAt, 3));
        }
    }

    /// WarpTimeManager's mapping, evaluated straight from the markers: linear
    /// between them and slope 1 outside. Reproduced rather than instantiated
    /// because the manager wants a clip and a clip wants a file, and what is
    /// being compared is the arithmetic.
    static double incumbentSourceSeconds(const std::vector<WarpMarker>& markers, double warpTime) {
        if (markers.empty())
            return warpTime;

        if (warpTime <= markers.front().warpTime)
            return markers.front().sourceTime + (warpTime - markers.front().warpTime);

        if (warpTime >= markers.back().warpTime)
            return markers.back().sourceTime + (warpTime - markers.back().warpTime);

        for (std::size_t index = 1; index < markers.size(); ++index) {
            const auto& previous = markers[index - 1];
            const auto& next = markers[index];
            if (warpTime > next.warpTime)
                continue;

            const auto span = next.warpTime - previous.warpTime;
            if (span <= 0.0)
                return previous.sourceTime;

            const auto proportion = (warpTime - previous.warpTime) / span;
            return previous.sourceTime + proportion * (next.sourceTime - previous.sourceTime);
        }

        return warpTime;
    }
};

static NullDiffMapTests nullDiffMapTests;

class TrimmedSessionLaunchTests : public juce::UnitTest {
  public:
    TrimmedSessionLaunchTests() : juce::UnitTest("Trimmed Session Launch", "magda") {}

    void runTest() override {
        beginTest("A render-start launch corrects a trimmed steady source");
        expectTrimmedLaunch(buildTrimmedSessionLaunchCase(nullDiffScratchDirectory()), 0);

        beginTest("Correction carries across callback blocks");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            value.blockSize = 64;
            expectTrimmedLaunch(value, 0);
        }

        beginTest("A launch inside a block corrects a trimmed steady source");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            value.launches.front().beat = 1.0;
            const auto launchSample =
                static_cast<int>(std::llround(value.sampleRate * 60.0 / value.startBpm()));
            expectTrimmedLaunch(value, launchSample);
        }

        beginTest("A zero correction length bypasses launch shaping");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            value.clips.front().launchFadeSamples = 0;
            expectTrimmedLaunch(value, 0);
        }

        beginTest("An untrimmed source attack is preserved");
        expectUntrimmedAttack(buildUntrimmedSessionAttackCase(nullDiffScratchDirectory()));

        beginTest("A trimmed arrangement clip is not launch-shaped");
        expectUnshapedTrim(buildTrimmedArrangementCase(nullDiffScratchDirectory()));

        beginTest("A reversed launch at its directional source edge preserves the attack");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            auto& clip = value.clips.front();
            clip.setPlacementBeats(0.0, 24.0);
            clip.deriveTimesFromBeats(value.startBpm());
            auto& event = *clip.primaryEvent();
            event.sourceAnchorSamples = 0;
            event.reversed = true;
            expectUnshapedTrim(value);
        }

        beginTest("A shortened reversed launch corrects its interior source edge");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            auto& event = *value.clips.front().primaryEvent();
            event.sourceAnchorSamples = 0;
            event.reversed = true;
            expectTrimmedLaunch(value, 0);
        }

        beginTest("A whole-loop phase at source zero preserves the source edge");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            auto& clip = value.clips.front();
            clip.loopEnabled = true;
            auto& event = *clip.primaryEvent();
            event.setLoopStartSeconds(0.0);
            event.setLoopLengthSeconds(1.0);
            event.setAnchorSeconds(1.0);
            expectUnshapedTrim(value);
        }

        beginTest("A loop whose mapped phase starts inside the source is corrected");
        {
            auto value = buildTrimmedSessionLaunchCase(nullDiffScratchDirectory());
            auto& clip = value.clips.front();
            clip.loopEnabled = true;
            auto& event = *clip.primaryEvent();
            event.setLoopStartSeconds(1.0);
            event.setLoopLengthSeconds(1.0);
            event.setAnchorSeconds(0.0);
            expectTrimmedLaunch(value, 0);
        }
    }

  private:
    template <typename Expected>
    void expectWaveform(const juce::AudioBuffer<float>& audio, Expected&& expected,
                        const juce::String& engine, int samples = -1) {
        expect(audio.getNumChannels() > 0, engine + ": no channels");
        expect(audio.getNumSamples() > 0, engine + ": empty render");
        if (audio.getNumChannels() == 0 || audio.getNumSamples() == 0)
            return;

        if (samples >= 0) {
            expect(audio.getNumSamples() >= samples, engine + ": expected at least " +
                                                         juce::String(samples) + " samples, got " +
                                                         juce::String(audio.getNumSamples()));
            if (audio.getNumSamples() < samples)
                return;
        }

        auto maxError = 0.0f;
        auto worstSample = 0;
        auto worstChannel = 0;
        const auto compared =
            samples < 0 ? audio.getNumSamples() : std::min(samples, audio.getNumSamples());
        for (auto channel = 0; channel < audio.getNumChannels(); ++channel) {
            for (auto sample = 0; sample < compared; ++sample) {
                const auto actual = audio.getSample(channel, sample);
                const auto error = std::isfinite(actual) ? std::abs(actual - expected(sample))
                                                         : std::numeric_limits<float>::infinity();
                if (error > maxError) {
                    maxError = error;
                    worstSample = sample;
                    worstChannel = channel;
                }
            }
        }

        expect(maxError <= 1.0e-6f,
               engine + ": max error " + juce::String(maxError, 9) + " at sample " +
                   juce::String(worstSample) + " channel " + juce::String(worstChannel) +
                   ", expected " + juce::String(expected(worstSample), 9) + ", got " +
                   juce::String(audio.getSample(worstChannel, worstSample), 9));
    }

    void expectTrimmedLaunch(const Case& value, int launchSample) {
        const auto length = value.clips.front().launchFadeSamples;
        const auto expected = [launchSample, length](int sample) {
            if (sample < launchSample)
                return 0.0f;

            const auto elapsed = sample - launchSample;
            if (length == 0 || elapsed >= length)
                return 0.5f;
            if (length == 1)
                return 0.0f;

            const auto phase = static_cast<float>(elapsed) / static_cast<float>(length - 1);
            const auto correction =
                0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * phase));
            return 0.5f * (1.0f - correction);
        };
        renderAndExpect(value, expected);
    }

    void expectUntrimmedAttack(const Case& value) {
        renderAndExpect(
            value, [](int sample) { return sample == 0 ? 1.0f : 0.0f; }, 2);
    }

    void expectUnshapedTrim(const Case& value) {
        renderAndExpect(value, [](int) { return 0.5f; });
    }

    template <typename Expected>
    void renderAndExpect(const Case& value, Expected&& expected, int samples = -1) {
        auto& assertionWatch = magda::test::AssertionWatch::instance();
        assertionWatch.take();

        const auto native = renderNative(value);
        const auto incumbent = renderIncumbent(value);
        const auto assertions = assertionWatch.take();
        for (const auto& assertion : assertions)
            expect(false, "asserted while rendering: " + assertion);

        expect(native.failure.empty(), "native render: " + juce::String(native.failure));
        expect(incumbent.failure.empty(), "incumbent render: " + juce::String(incumbent.failure));
        expect(native.diagnostics.empty(), "native render produced diagnostics");
        expect(native.starvedVoices == 0,
               "native render starved " + juce::String(native.starvedVoices) + " voices");
        expect(native.droppedMidiEvents == 0,
               "native render dropped " + juce::String(native.droppedMidiEvents) + " MIDI events");
        expect(incumbent.renderedInFloat, "incumbent render was not floating point");
        if (!native.failure.empty() || !incumbent.failure.empty())
            return;

        const auto expectedSamples = static_cast<int>(std::llround(
            (value.endBeat - value.startBeat) * 60.0 / value.startBpm() * value.sampleRate));
        expect(native.audio.getNumSamples() == expectedSamples,
               "native render length " + juce::String(native.audio.getNumSamples()) +
                   ", expected " + juce::String(expectedSamples));
        expect(incumbent.audio.getNumSamples() == expectedSamples,
               "incumbent render length " + juce::String(incumbent.audio.getNumSamples()) +
                   ", expected " + juce::String(expectedSamples));

        expectWaveform(native.audio, expected, "native", samples);
        expectWaveform(incumbent.audio, expected, "incumbent", samples);
    }
};

static TrimmedSessionLaunchTests trimmedSessionLaunchTests;

// =============================================================================
// The corpus
// =============================================================================

class NullDiffCorpusTests : public juce::UnitTest {
  public:
    NullDiffCorpusTests() : juce::UnitTest("Null Diff Corpus", "magda") {}

    void runTest() override {
        beginTest("Every case, through both engines");

        const auto corpus = sharedCorpus(nullDiffScratchDirectory());
        const auto run = runSuite(corpus, [this](const juce::String& line) { logMessage(line); });

        // Printed on every run rather than only on failure. Numbers that move
        // are the point: a corpus that stays quiet cannot show a residual
        // creeping from -138 dB to -122 dB, which is what an engine going
        // subtly wrong looks like before it goes audibly wrong.
        const auto report = formatReport(
            run.reports, corpus.empty() ? CaseEnvironment{} : corpus.front().environment());
        logMessage(report);
        std::cout << report << std::endl;

        // What the corpus asserts today, and what it is still calibrating.
        //
        // Every case runs, every case is measured, and every measurement is
        // printed above whatever happens here. What this list changes is which
        // of them the build is held to: a case under calibration has a number
        // but not yet a bound with a mechanism behind it, and asserting a bound
        // nobody has justified is how a real difference ends up inside an
        // expected one.
        //
        // It is a set, not a skip. Membership is asserted in both directions,
        // so a case that starts failing has to be added here by somebody with a
        // reason, and a case that starts holding has to be taken out. Neither
        // can happen quietly, which is the same rule the block-size list lives
        // by.
        //
        // Where each one stands, from the run that produced these numbers:
        //
        //  - rate.48k sits -0.852 of a sample out, which looked like the
        //    difference between two interpolation kernels until the corpus
        //    tried it: aligning by that number makes the case worse, and a
        //    fixed offset is the one thing a single number can undo. So the
        //    mechanism is not a constant delay, and no bound goes in until
        //    something explains it.
        //  - fades.speedramp sits 20.6 samples out, which is too large to be a
        //    kernel and wants the same treatment rate.48k just had: find the
        //    mechanism, then pin it.
        //  - the stretched cases align and compare, and stretch.signalsmith
        //    already agrees to a spectral median of 0.46 dB, which says the two
        //    really are the same sound. Their envelope lock is not yet reliable
        //    on all of them, because the search window holds too few swells to
        //    be sure of, and no bound goes in until it is.
        //  - session.launch.sustained sits at -110 dB, and it is the fork's
        //    launcher path that is inexact rather than anything moving: the
        //    engine emits a held 0.5 bit for bit, the fork wobbles by one or
        //    two ULP per sample, and the same material on an arrangement clip
        //    nulls at -inf. Its reported shift is not a shift. A held level has
        //    nothing to correlate, so the aligner returns whatever the noise
        //    prefers -- which is what -23 samples on the trimmed variant of
        //    this case was, and #2458 read that as a trim landing in the wrong
        //    place. No bound until the fork's own arithmetic is named.
        const std::set<std::string> underCalibration{
            "rate.48k",
            "fades.speedramp",
            "tempo.auto",
            "warp.audio",
            "stretch.signalsmith",
            "stretch.soundtouch.normal",
            "stretch.soundtouch.better",
            "session.launch.sustained",
        };

        const auto complaints =
            judgeSuite(run.asserted, run.unmeasurable, run.failing, underCalibration, run.notRun);

        const auto join = [](const std::vector<std::string>& names) {
            juce::StringArray parts;
            for (const auto& name : names)
                parts.add(juce::String(name));
            return parts.joinIntoString(", ");
        };

        // Every case reached a verdict. Asserted positively rather than left
        // implied, because the three checks below all pass on an empty run, and
        // a corpus that rendered nothing would otherwise look exactly like a
        // corpus that agreed about everything.
        expect(run.reports.size() == corpus.size(), "expected " + juce::String((int)corpus.size()) +
                                                        " reports, got " +
                                                        juce::String((int)run.reports.size()));

        // Assertions first, and outside the calibration list entirely. A case
        // that is expected to fail would otherwise satisfy that expectation by
        // asserting, and the invalid graph this exists to catch would be green
        // across the eight names above.
        expect(complaints.asserted.empty(),
               "asserted while rendering, which is never a result the calibration list forgives: " +
                   join(complaints.asserted));

        // The same rule, for the other way a case can produce no number: the
        // list forgives a comparison without a bound, never an inability to
        // measure one.
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
};

static NullDiffCorpusTests nullDiffCorpusTests;

// =============================================================================
// What the suite is held to
// =============================================================================

/**
 * The calibration list forgives a comparison, never an assertion.
 *
 * Eight cases are expected to fail today, and the expectation is asserted in
 * both directions so neither a new failure nor a fixed one passes quietly. That
 * arrangement has one hole worth a test of its own: an assertion inside one of
 * those eight satisfies the same expectation as the comparison failure it was
 * forgiven for, so the invalid graph the watch was added to catch would be green
 * across a third of the corpus.
 */
class NullDiffSuiteRuleTests : public juce::UnitTest {
  public:
    NullDiffSuiteRuleTests() : juce::UnitTest("Null Diff Suite Rules", "magda") {}

    void runTest() override {
        const std::set<std::string> calibrating{"tempo.auto", "warp.audio"};

        beginTest("A calibrating case that asserts still fails the suite");
        {
            // It is failing, which is what the list expects of it, so the
            // membership check is satisfied and says nothing. The assertion has
            // to be what fails the suite, on its own.
            const auto complaints = judgeSuite({"tempo.auto"}, {"tempo.auto"},
                                               {"tempo.auto", "warp.audio"}, calibrating, {});

            expect(complaints.asserted == std::vector<std::string>{"tempo.auto"});
            expect(complaints.unmeasurable.empty(),
                   "an assertion is that same failure, named better");
            expect(complaints.unexpectedFailures.empty(), "it is on the list, so not unexpected");
            expect(complaints.nowHolding.empty(), "both are still failing");
            expect(!complaints.empty(), "the suite has to complain about something");
        }

        beginTest("A calibrating case that could not be measured still fails the suite");
        {
            // A proxy that never arrived, a leg that would not render, a
            // read-back that was not float, a comparator refusal, a length
            // mismatch. Every one leaves the case failing with nothing measured,
            // which is what the expected-failure membership would otherwise
            // swallow. The list forgives a comparison without a bound, never the
            // absence of a comparison.
            const auto complaints =
                judgeSuite({}, {"tempo.auto"}, {"tempo.auto", "warp.audio"}, calibrating, {});

            expect(complaints.unmeasurable == std::vector<std::string>{"tempo.auto"});
            expect(complaints.unexpectedFailures.empty(), "it is on the list, so not unexpected");
            expect(complaints.nowHolding.empty(), "both are still failing");
            expect(!complaints.empty(), "the suite has to complain about something");
        }

        beginTest("A calibrating case that merely fails says nothing");
        {
            const auto complaints =
                judgeSuite({}, {}, {"tempo.auto", "warp.audio"}, calibrating, {});
            expect(complaints.empty(), "the run everybody expects");
        }

        beginTest("A case that asserts outside the list fails too");
        {
            const auto complaints = judgeSuite(
                {"mix.pan"}, {"mix.pan"}, {"mix.pan", "tempo.auto", "warp.audio"}, calibrating, {});

            expect(complaints.asserted == std::vector<std::string>{"mix.pan"});
            expect(complaints.unexpectedFailures == std::vector<std::string>{"mix.pan"});
        }

        beginTest("A calibrating case that starts holding has to come off the list");
        {
            const auto complaints = judgeSuite({}, {}, {"warp.audio"}, calibrating, {});
            expect(complaints.nowHolding == std::vector<std::string>{"tempo.auto"});
        }

        beginTest("A calibrating case that did not run stays on the list");
        {
            // What a machine without the case's plugin sees, CI included. Not
            // running is not holding, and reading it as holding would take the
            // entry off wherever the corpus is thinnest.
            const auto complaints = judgeSuite({}, {}, {"warp.audio"}, calibrating, {"tempo.auto"});
            expect(complaints.nowHolding.empty(), "it did not run, so it did not hold");
            expect(complaints.empty(), "and nothing else is wrong with that run");
        }
    }
};

static NullDiffSuiteRuleTests nullDiffSuiteRuleTests;
