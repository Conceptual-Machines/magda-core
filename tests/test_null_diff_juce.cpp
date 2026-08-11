#include <juce_core/juce_core.h>

#include <cmath>
#include <iostream>
#include <set>
#include <string>

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

        const auto corpus = buildCorpus(scratchDirectory());
        std::vector<CaseReport> reports;

        for (const auto& value : corpus)
            reports.push_back(run(value));

        // Printed on every run rather than only on failure. Numbers that move
        // are the point: a corpus that stays quiet cannot show a residual
        // creeping from -138 dB to -122 dB, which is what an engine going
        // subtly wrong looks like before it goes audibly wrong.
        const auto report =
            formatReport(reports, corpus.empty() ? 44100.0 : corpus.front().sampleRate,
                         corpus.empty() ? 512 : corpus.front().blockSize);
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
        const std::set<std::string> underCalibration{
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
        for (const auto& value : reports)
            if (!value.passed)
                failing.insert(value.name);

        for (const auto& value : reports)
            if (underCalibration.count(value.name) == 0)
                expect(value.passed, juce::String(value.name) + " did not hold");

        for (const auto& name : underCalibration)
            expect(failing.count(name) == 1,
                   juce::String(name) + " now holds and should come off the calibration list");
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
        report.verdict = value.verdict;

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
        if (!native.diagnostics.empty()) {
            report.unmeasurable =
                "the engine could not honour the case: " + native.diagnostics.front();
            return report;
        }
        if (!incumbent.renderedInFloat) {
            // Sixteen bits would put quantisation noise well above the floor and
            // every case would be measuring the file format.
            report.unmeasurable = "the incumbent render came back as fixed point";
            return report;
        }

        if (value.capturesMidi()) {
            MidiCompareOptions options;
            options.sampleRate = value.sampleRate;
            options.bpm = value.startBpm();
            options.noteShiftSamples = static_cast<std::int64_t>(std::llround(
                value.declaredMidiShiftBeats * 60.0 / value.startBpm() * value.sampleRate));
            options.incumbentNoteEndEarlySamples = static_cast<int>(
                std::llround(value.incumbentNoteEndEarlySeconds * value.sampleRate));

            report.hasMidi = true;
            report.midi = compareMidi(native.midi, incumbent.midi, options);
            report.passed = report.midi.passed();

            if (!report.passed)
                for (const auto& problem : report.midi.problems)
                    logMessage("  " + juce::String(value.name) + ": " + problem);

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
        const auto searchRange = value.verdict == Verdict::Shift ? value.maxShiftSamples : 256;
        const auto estimate = estimateShift(native.audio, incumbent.audio, searchRange);
        report.hasMeasuredShift = estimate.found;
        report.shiftCorrelation = estimate.correlation;
        report.shiftCorrelationEnvelope = estimate.envelopeCorrelation;

        // A stretched case takes the envelope's answer, everything else the
        // waveform's, because those are the two things each can be aligned by.
        report.measuredShift =
            value.verdict == Verdict::Shift ? estimate.envelopeSamples : estimate.fractionalSamples;

        // A declared sub-sample offset is undone before anything is measured.
        // Not a tolerance: if the offset were not the fixed thing the case
        // claims, one number could not undo it and the null below would not
        // arrive.
        const auto aligned =
            value.declaredFractionalShiftSamples != 0.0
                ? delayFractionally(native.audio, -value.declaredFractionalShiftSamples)
                : native.audio;

        AudioCompareOptions options;
        options.floorDb = value.floorDb;
        options.sampleRate = value.sampleRate;
        options.measureShift = value.verdict == Verdict::Shift;
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

        switch (value.verdict) {
            case Verdict::Null:
                report.passed = report.audio.nulled();
                break;

            case Verdict::Shift:
                report.passed = judgeStretched(value, native, incumbent, report);
                break;

            case Verdict::ReportOnly:
                // Measured and printed. What it says is how far apart the two
                // stretchers are on material that has everything in it, which
                // is a number worth watching and not a claim about playback.
                //
                // That it was measurable at all is asked above, with the same
                // two questions every other verdict gets, so this really is the
                // only thing left for it to decide.
                report.passed = true;
                break;

            case Verdict::Midi:
                break;
        }

        if (!report.passed)
            writeArtefacts(value.name, native.audio, incumbent.audio, value.sampleRate);

        return report;
    }
};

static NullDiffCorpusTests nullDiffCorpusTests;
