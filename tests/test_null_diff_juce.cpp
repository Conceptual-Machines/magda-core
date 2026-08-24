#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>

#include "AssertionWatch.hpp"
#include "NullDiffCompare.hpp"
#include "NullDiffNativeLeg.hpp"
#include "NullDiffTeLeg.hpp"
#include "SharedTestEngine.hpp"
#include "clip/WarpMap.hpp"
#include "transport/TempoMap.hpp"

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

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

namespace {

juce::File scratchDirectory() {
    auto root =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("magda_null_diff");
    root.createDirectory();
    return root;
}

/// The largest magnitude anywhere in @p buffer, which is all the silence guard
/// below needs: it asks whether a render happened, not how loud it was.
float peakOf(const juce::AudioBuffer<float>& buffer) {
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    return peak;
}

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
                           const std::set<std::string>& underCalibration) {
    SuiteComplaints complaints;

    complaints.asserted.assign(asserted.begin(), asserted.end());

    for (const auto& name : unmeasurable)
        if (asserted.count(name) == 0)
            complaints.unmeasurable.push_back(name);

    for (const auto& name : failing)
        if (underCalibration.count(name) == 0)
            complaints.unexpectedFailures.push_back(name);

    for (const auto& name : underCalibration)
        if (failing.count(name) == 0)
            complaints.nowHolding.push_back(name);

    return complaints;
}

/// Where the artefacts of a failing case go. A parity failure is diagnosed by
/// listening and by looking, and a test that only says "expected 1e-12, got
/// 0.3" makes that the reader's problem.
void writeArtefacts(const std::string& name, const juce::AudioBuffer<float>& native,
                    const juce::AudioBuffer<float>& incumbent, double sampleRate) {
    const auto directory = scratchDirectory().getChildFile("failures");
    directory.createDirectory();

    const auto write = [&](const juce::String& suffix, const juce::AudioBuffer<float>& buffer) {
        if (buffer.getNumSamples() == 0)
            return juce::File();

        const auto file = directory.getChildFile(juce::String(name) + "." + suffix + ".wav");
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
        if (stream == nullptr)
            return juce::File();

        const auto options =
            juce::AudioFormatWriterOptions{}
                .withSampleRate(sampleRate)
                .withNumChannels(buffer.getNumChannels())
                .withBitsPerSample(32)
                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        if (auto writer = format.createWriterFor(stream, options))
            writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());

        return file;
    };

    write("native", native);
    write("incumbent", incumbent);

    if (native.getNumChannels() == incumbent.getNumChannels()) {
        juce::AudioBuffer<float> residual(
            native.getNumChannels(), std::min(native.getNumSamples(), incumbent.getNumSamples()));
        for (auto channel = 0; channel < residual.getNumChannels(); ++channel)
            for (auto sample = 0; sample < residual.getNumSamples(); ++sample)
                residual.setSample(channel, sample,
                                   native.getSample(channel, sample) -
                                       incumbent.getSample(channel, sample));
        write("residual", residual);
    }
}

}  // namespace

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

// =============================================================================
// The corpus
// =============================================================================

class NullDiffCorpusTests : public juce::UnitTest {
  public:
    NullDiffCorpusTests() : juce::UnitTest("Null Diff Corpus", "magda") {}

    void runTest() override {
        beginTest("Every case, through both engines");

        const auto corpus = sharedCorpus(scratchDirectory());
        std::vector<CaseReport> reports;

        // Read per case, so an assertion is attributed to the case that
        // provoked it rather than to the run. Taken once before the walk begins
        // to drop anything logged on the way in, which belongs to whatever ran
        // before this test rather than to its first case.
        auto& watch = magda::test::AssertionWatch::instance();
        watch.take();

        // Kept apart from pass and fail on purpose. A case under calibration is
        // expected to fail, so an assertion inside one would be indistinguishable
        // from the failure it was already forgiven for.
        std::set<std::string> asserted;

        for (const auto& value : corpus) {
            auto report = run(value);

            // An engine that objects to its own graph has not produced a render
            // worth comparing, whatever the residual says. Reported as
            // unmeasurable rather than as a residual, for the same reason a
            // proxy that never arrived is: the number would be about something
            // other than parity, and it would look like a parity failure.
            if (auto fired = watch.take(); !fired.empty()) {
                report.passed = false;
                report.unmeasurable =
                    "the engine asserted while rendering: " + fired.front().toStdString() +
                    (fired.size() > 1 ? " (and " + std::to_string(fired.size() - 1) + " more)"
                                      : "");
                asserted.insert(value.name);

                for (const auto& assertion : fired)
                    logMessage("  " + juce::String(value.name) + ": " + assertion);
            }

            reports.push_back(std::move(report));
        }

        // Printed on every run rather than only on failure. Numbers that move
        // are the point: a corpus that stays quiet cannot show a residual
        // creeping from -138 dB to -122 dB, which is what an engine going
        // subtly wrong looks like before it goes audibly wrong.
        const auto report = formatReport(reports, corpus.empty() ? CaseEnvironment{}
                                                                 : corpus.front().environment());
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
        //  - speed.ratio does not correlate at any offset with no shift at all,
        //    so what differs is content rather than timing: the two disagree
        //    about what a speed ratio means with stretching switched off. That
        //    is a finding about the engine, not about the corpus.
        //  - fades.speedramp sits 20.6 samples out, which is too large to be a
        //    kernel and wants the same treatment rate.48k just had: find the
        //    mechanism, then pin it.
        //  - the stretched cases align and compare, and stretch.signalsmith
        //    already agrees to a spectral median of 0.46 dB, which says the two
        //    really are the same sound. Their envelope lock is not yet reliable
        //    on all of them, because the search window holds too few swells to
        //    be sure of, and no bound goes in until it is.
        //  - rack.nested does not fail over a bound at all: the incumbent
        //    renders a nested rack as if it were not there. RackInstance's own
        //    applyToBuffer is empty and the processing lives in
        //    RackInstanceNode, which only EditNodeBuilder substitutes; the rack
        //    graph's own node builder wraps every plugin it holds in a plain
        //    PluginNode, so a rack inside a rack is built, connected and then
        //    passed straight through. The engine processes it, so the case is
        //    the engine being right rather than a bound nobody has justified,
        //    and it comes off this list when the fork is fixed (#2171).
        const std::set<std::string> underCalibration{
            "rack.nested",
            "rate.48k",
            "speed.ratio",
            "fades.speedramp",
            "tempo.auto",
            "warp.audio",
            "stretch.signalsmith",
            "stretch.soundtouch.normal",
            "stretch.soundtouch.better",
        };

        std::set<std::string> failing;
        std::set<std::string> unmeasurable;
        for (const auto& value : reports) {
            if (!value.passed)
                failing.insert(value.name);
            if (!value.unmeasurable.empty())
                unmeasurable.insert(value.name);
        }

        const auto complaints = judgeSuite(asserted, unmeasurable, failing, underCalibration);

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
        expect(reports.size() == corpus.size(), "expected " + juce::String((int)corpus.size()) +
                                                    " reports, got " +
                                                    juce::String((int)reports.size()));

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

  private:
    /**
     * @brief A stretched case, on the three things that can be asserted of one.
     *
     * Not a waveform null, and not because the bar was lowered. Two vocoders
     * primed differently never converge on one waveform: priming sets the
     * initial phase state, phase in a vocoder is memory, and the two legs prime
     * from different material. Magnitude is what framing leaves intact.
     *
     * Magnitude alone would let a wrong ratio, a misplaced clip or a dropped
     * block through, so it is one of three rather than a replacement:
     *
     *  - the pinned shift, measured by cross correlation and checked against
     *    what the stretcher says it primes with, so an offset that appeared
     *    because a clip moved is not quietly absorbed;
     *  - envelope timing after that shift, which is what keeps a placement bug
     *    visible when the waveform cannot be compared;
     *  - the magnitude spectrogram, window and hop stated, bound declared by
     *    the case with its mechanism.
     */
    bool judgeStretched(const Case& value, const NativeRender& native,
                        const IncumbentRender& incumbent, CaseReport& report) {
        report.hasSpectral = true;
        report.primingSamples = native.primingSamples;

        // The alignment comes from the envelopes, not from the waveforms. Two
        // vocoders never correlate as waveforms however well aligned they are,
        // so requiring that here would fail every stretched case for being what
        // it was predicted to be. The envelope is what survives the phase
        // difference, and the material these cases play has one to correlate.
        if (report.shiftCorrelationEnvelope < 0.98) {
            logMessage("  " + juce::String(value.name) + ": the envelopes do not correlate (" +
                       juce::String(report.shiftCorrelationEnvelope, 3) + ") at any offset");
            return false;
        }

        const auto shift = static_cast<int>(std::llround(report.measuredShift));

        report.envelope = compareEnvelopes(native.audio, incumbent.audio, shift, value.sampleRate);
        report.spectra = compareSpectra(native.audio, incumbent.audio, shift);

        auto held = true;

        // A shift nobody predicted is not a shift the corpus can certify. The
        // alignment is measured from the material, so without something to
        // check it against it would absorb a clip in the wrong place and the
        // envelope and spectrum would then agree about the wrong thing
        // perfectly well.
        if (value.expectedShiftSamples == 0) {
            logMessage("  " + juce::String(value.name) +
                       ": no predicted shift is declared, so the measured " +
                       juce::String(report.measuredShift, 0) +
                       " cannot be told from a clip in the wrong place. The engine primed its "
                       "stretcher with " +
                       juce::String(native.primingSamples) + " samples.");
            return false;
        }

        // The shift against the prediction. The engine reports what its own
        // stretcher primes with, and the fork is late by its own copy of the
        // same library, so the two figures are the same figure.
        {
            const auto allowed =
                std::max(64.0, std::abs(value.expectedShiftSamples) * value.shiftTolerance);
            if (std::abs(report.measuredShift - value.expectedShiftSamples) > allowed) {
                logMessage("  " + juce::String(value.name) + ": shift " +
                           juce::String(report.measuredShift, 1) + " against a predicted " +
                           juce::String(value.expectedShiftSamples));
                held = false;
            }
        }

        if (std::abs(report.envelope.lagSamples) > value.envelopeToleranceSamples) {
            logMessage("  " + juce::String(value.name) + ": the envelopes are " +
                       juce::String(report.envelope.lagSamples, 2) +
                       " samples apart after the shift");
            held = false;
        }

        if (report.spectra.frames == 0) {
            logMessage("  " + juce::String(value.name) + ": nothing to compare spectrally");
            held = false;
        } else if (value.spectralPercentile95Db > 0.0 &&
                   report.spectra.percentile95Db > value.spectralPercentile95Db) {
            logMessage("  " + juce::String(value.name) + ": spectral p95 " +
                       juce::String(report.spectra.percentile95Db, 2) + " dB against a bound of " +
                       juce::String(value.spectralPercentile95Db, 2));
            held = false;
        }

        return held;
    }

    CaseReport run(const Case& value) {
        CaseReport report;
        report.name = value.name;
        report.tier = value.tier;
        report.environment = value.environment();

        const auto native = renderNative(value);
        const auto incumbent = renderIncumbent(value);

        // A leg that could not render is never reported as a residual.
        if (!native.failure.empty()) {
            report.unmeasurable = "native leg: " + native.failure;
            return report;
        }
        if (!incumbent.failure.empty()) {
            report.unmeasurable = "incumbent leg: " + incumbent.failure;
            return report;
        }
        // The diagnostics this case declared it expects are struck off first,
        // and each has to have been reported: a case that names one and does not
        // get it is measuring a plan that no longer refuses what the case is
        // about (NullDiffCase.hpp).
        auto diagnostics = native.diagnostics;
        for (const auto& expected : value.expectedDiagnostics) {
            const auto found =
                std::find_if(diagnostics.begin(), diagnostics.end(), [&](const std::string& value) {
                    return value.find(expected) != std::string::npos;
                });

            if (found == diagnostics.end()) {
                report.unmeasurable =
                    "the case expects the plan to report \"" + expected + "\", and it did not";
                return report;
            }

            diagnostics.erase(found);
        }

        if (!diagnostics.empty()) {
            report.unmeasurable = "the engine could not honour the case: " + diagnostics.front();
            return report;
        }
        if (!incumbent.renderedInFloat) {
            // Sixteen bits would put quantisation noise well above the floor and
            // every case would be measuring the file format.
            report.unmeasurable = "the incumbent render came back as fixed point";
            return report;
        }

        // The two questions, asked independently. A project with an instrument
        // track and audio tracks answers both, and a case that answers neither
        // is refused below rather than passing by having claimed nothing.
        auto midiHeld = true;

        if (value.capturesMidi()) {
            MidiCompareOptions options;
            options.sampleRate = value.sampleRate;
            options.bpm = value.startBpm();
            options.noteShiftSamples = static_cast<std::int64_t>(std::llround(
                value.declaredMidiShiftBeats * 60.0 / value.startBpm() * value.sampleRate));
            options.incumbentNoteEndEarlySamples = static_cast<int>(
                std::llround(value.incumbentNoteEndEarlySeconds * value.sampleRate));

            report.hasMidi = true;

            // Start true and narrow, since the printed line is the conjunction
            // over the tracks. The struct's own defaults are false, which is the
            // right answer for a comparison nobody ran and the wrong start for
            // one being accumulated.
            report.midi.notesMatch = true;
            report.midi.controllersMatch = true;
            report.midi.otherMessagesMatch = true;

            // Compared per track, not as one aggregate. A MidiEvent carries
            // nothing but its bytes and its position, so two instrument tracks
            // that received each other's notes produce the same flat stream as
            // two that received their own: comparing the aggregate would certify
            // a capture landing on the wrong track. The report still prints one
            // note count, because that is what a reader wants; the judgement is
            // made where the identity survives.
            std::set<TrackId> tracks;
            for (const auto& [trackId, stream] : native.midiByTrack)
                tracks.insert(trackId);
            for (const auto& [trackId, stream] : incumbent.midiByTrack)
                tracks.insert(trackId);

            midiHeld = !tracks.empty();

            for (const auto trackId : tracks) {
                const auto nativeStream = native.midiByTrack.find(trackId);
                const auto incumbentStream = incumbent.midiByTrack.find(trackId);

                // A track one leg captured and the other did not is the failure
                // this split exists to see, and comparing against an empty
                // stream would report it as every note missing rather than as
                // the capture that was never placed.
                if (nativeStream == native.midiByTrack.end() ||
                    incumbentStream == incumbent.midiByTrack.end()) {
                    midiHeld = false;
                    logMessage("  " + juce::String(value.name) + ": track " +
                               juce::String(trackId) + " was captured by " +
                               (nativeStream == native.midiByTrack.end() ? "the incumbent"
                                                                         : "the engine") +
                               " only");
                    continue;
                }

                const auto compared =
                    compareMidi(nativeStream->second, incumbentStream->second, options);

                // Accumulated so the printed line covers the whole project.
                report.midi.notesCompared += compared.notesCompared;
                report.midi.notesMatch = report.midi.notesMatch && compared.notesMatch;
                report.midi.controllersMatch =
                    report.midi.controllersMatch && compared.controllersMatch;
                report.midi.otherMessagesMatch =
                    report.midi.otherMessagesMatch && compared.otherMessagesMatch;

                if (!compared.passed()) {
                    midiHeld = false;
                    for (const auto& problem : compared.problems)
                        logMessage("  " + juce::String(value.name) + ": track " +
                                   juce::String(trackId) + ": " + problem);
                }
            }
        }

        if (value.tier == AudioTier::None) {
            if (!value.capturesMidi()) {
                report.unmeasurable = "the case asserts nothing: no audio tier and no MIDI";
                return report;
            }

            report.passed = midiHeld;
            return report;
        }

        // Two silences agree. Every comparison below measures how far the two
        // renders sit apart and nothing measures whether either of them is a
        // render at all, so a case that reaches the master with nothing on it
        // nulls perfectly and asserts nothing -- and it does so at the ordinary
        // floor, printed as an ordinary pass, which is the one failure a
        // null-diff corpus cannot see by reading its own report.
        //
        // The corpus already knew this by hand: rack.aux carries an audible
        // chain beside the one it expects to be dropped, "so the assertion is a
        // comparison instead of two silences agreeing". This is that sentence
        // enforced rather than repeated per case.
        //
        // What it does not reach is a path silenced inside a render that is not
        // silent, and that is worth writing down because it is what actually
        // happened here: the send cases were first written with the source hard
        // left and the return hard right, and a post-fader tap is taken after
        // the fader, which is where the pan is applied -- so the return was
        // handed a hard-left signal and panned it away. The dry path was still
        // audible, so this guard would have passed them. Catching that needs a
        // case to declare what level it expects to render, which is a change to
        // every case in the corpus and not this one's. Until then it is the
        // reason the send cases are read as one total rather than by channel.
        //
        // Both legs, because either one alone would let the other's silence
        // through, and a case where one leg renders nothing is a difference the
        // residual below already reports loudly.
        if (peakOf(native.audio) <= 0.0f && peakOf(incumbent.audio) <= 0.0f) {
            report.unmeasurable = "the case asserts nothing: both renders are silent";
            return report;
        }

        // Measured on every audio case, applied only where one is declared. An
        // offset that is not applied is still the first thing worth knowing
        // about a case that will not null, and measuring it is what turns "the
        // residual is -32 dB" into "the two are three quarters of a sample
        // apart", which is a different conversation.
        // A narrow search where no shift is expected: it is a diagnosis rather
        // than an alignment, and the answer to "how far apart are these" is a
        // sample or two or it is not the question.
        const auto searchRange = value.tier == AudioTier::Spectral ? value.maxShiftSamples : 256;
        const auto estimate = estimateShift(native.audio, incumbent.audio, searchRange);
        report.hasMeasuredShift = estimate.found;
        report.shiftCorrelation = estimate.correlation;
        report.shiftCorrelationEnvelope = estimate.envelopeCorrelation;

        // A stretched case takes the envelope's answer, everything else the
        // waveform's, because those are the two things each can be aligned by.
        report.measuredShift = value.tier == AudioTier::Spectral ? estimate.envelopeSamples
                                                                 : estimate.fractionalSamples;

        // A declared sub-sample offset is undone before anything is measured.
        // Not a tolerance: if the offset were not the fixed thing the case
        // claims, one number could not undo it and the null below would not
        // arrive.
        const auto aligned =
            value.declaredFractionalShiftSamples != 0.0
                ? delayFractionally(native.audio, -value.declaredFractionalShiftSamples)
                : native.audio;

        // The invariants tier never asks for a residual, so it never asks for an
        // alignment either: what it checks is a property of each render on its
        // own, and sliding one against the other would change neither answer.
        if (value.tier == AudioTier::Invariants) {
            InvariantOptions options;
            options.sampleRate = value.sampleRate;
            options.maxStepPerSample = value.maxStepPerSample;

            report.hasInvariants = true;
            report.invariants = compareInvariants(native.audio, incumbent.audio, options);
            report.passed = report.invariants.passed() && midiHeld;

            if (!report.invariants.passed()) {
                if (!report.invariants.refusal.empty())
                    report.unmeasurable = report.invariants.refusal;
                for (const auto& problem : report.invariants.problems)
                    logMessage("  " + juce::String(value.name) + ": " + problem);
                writeArtefacts(value.name, native.audio, incumbent.audio, value.sampleRate);
            }

            return report;
        }

        AudioCompareOptions options;
        options.floorDb = value.floorDb;
        options.sampleRate = value.sampleRate;
        options.measureShift = value.tier == AudioTier::Spectral;
        options.maxShiftSamples = value.maxShiftSamples;

        report.hasAudio = true;
        report.audio = compareAudio(aligned, incumbent.audio, options);

        // Before any verdict, and the same two questions for all of them. Every
        // measurement below is taken over what the two renders both cover, so a
        // leg that came back short agrees everywhere anybody looks: a null sees
        // an identical prefix, and the stretched metrics trim to the same
        // overlap. The length is the only thing that says otherwise, and a
        // render that came back short is a bug in a leg whatever the case was
        // asking about.
        if (!report.audio.refusal.empty()) {
            report.unmeasurable = report.audio.refusal;
            return report;
        }

        if (report.audio.lengthDifference != 0) {
            report.unmeasurable =
                "the renders differ in length by " +
                std::to_string(static_cast<long long>(report.audio.lengthDifference)) + " samples";
            return report;
        }

        switch (value.tier) {
            case AudioTier::Exact:
                report.passed = report.audio.nulled();
                break;

            case AudioTier::Aligned:
                // One pinned offset, then the same null everything else is held
                // to. The offset is declared rather than fitted, and undone
                // above before anything was measured, so the null arriving is
                // the evidence that the mechanism is the fixed thing the case
                // says it is. A case that declared no offset has aligned by
                // nothing and is refused rather than quietly judged as Exact.
                if (value.declaredFractionalShiftSamples == 0.0) {
                    report.unmeasurable = "an aligned case with no declared offset";
                    return report;
                }
                report.passed = report.audio.nulled();
                break;

            case AudioTier::Spectral:
                report.passed = judgeStretched(value, native, incumbent, report);
                break;

            case AudioTier::Measured:
                // Measured and printed. What it says is how far apart the two
                // stretchers are on material that has everything in it, which
                // is a number worth watching and not a claim about playback.
                //
                // That it was measurable at all is asked above, with the same
                // two questions every other tier gets, so this really is the
                // only thing left for it to decide.
                report.passed = true;
                break;

            case AudioTier::Invariants:
            case AudioTier::None:
                // Both returned above.
                break;
        }

        report.passed = report.passed && midiHeld;

        if (!report.passed)
            writeArtefacts(value.name, native.audio, incumbent.audio, value.sampleRate);

        return report;
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
                                               {"tempo.auto", "warp.audio"}, calibrating);

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
                judgeSuite({}, {"tempo.auto"}, {"tempo.auto", "warp.audio"}, calibrating);

            expect(complaints.unmeasurable == std::vector<std::string>{"tempo.auto"});
            expect(complaints.unexpectedFailures.empty(), "it is on the list, so not unexpected");
            expect(complaints.nowHolding.empty(), "both are still failing");
            expect(!complaints.empty(), "the suite has to complain about something");
        }

        beginTest("A calibrating case that merely fails says nothing");
        {
            const auto complaints = judgeSuite({}, {}, {"tempo.auto", "warp.audio"}, calibrating);
            expect(complaints.empty(), "the run everybody expects");
        }

        beginTest("A case that asserts outside the list fails too");
        {
            const auto complaints = judgeSuite(
                {"mix.pan"}, {"mix.pan"}, {"mix.pan", "tempo.auto", "warp.audio"}, calibrating);

            expect(complaints.asserted == std::vector<std::string>{"mix.pan"});
            expect(complaints.unexpectedFailures == std::vector<std::string>{"mix.pan"});
        }

        beginTest("A calibrating case that starts holding has to come off the list");
        {
            const auto complaints = judgeSuite({}, {}, {"warp.audio"}, calibrating);
            expect(complaints.nowHolding == std::vector<std::string>{"tempo.auto"});
        }
    }
};

static NullDiffSuiteRuleTests nullDiffSuiteRuleTests;
