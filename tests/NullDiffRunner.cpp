#include "NullDiffRunner.hpp"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include "AssertionWatch.hpp"
#include "NullDiffNativeLeg.hpp"
#include "NullDiffTeLeg.hpp"
#include "SharedTestEngine.hpp"

namespace magda::nulldiff {
namespace {

/**
 * @brief The one scan both legs resolve a project's plugins against.
 *
 * The incumbent leg resolves against the engine's own KnownPluginList, because
 * that is what the app does. The native leg is handed the same one rather than
 * a list of its own: two legs that each found their own copy of a plugin would
 * be rendering two projects and calling the difference an engine bug.
 */
InstalledPlugins installedPlugins() {
    auto* engine = magda::test::getSharedEngine().getEngine();
    if (engine == nullptr)
        return {};

    auto& plugins = engine->getPluginManager();
    return {.formats = &plugins.pluginFormatManager, .knownPlugins = &plugins.knownPluginList};
}

/**
 * @brief Look on this machine for the plugins @p wanted names, once each.
 *
 * The corpus finds its own plugins rather than reading the app's scan, and it
 * has to. A test binary runs under a sandboxed HOME (the Makefile's TEST_ENV),
 * so the application data directory the app keeps its KnownPluginList in is not
 * this machine's -- and it must not be, because a suite whose verdict depended
 * on what the developer last clicked in a scan dialog is not a suite. Left
 * alone, the list is empty on every machine, and every project hosting a plugin
 * would be reported not run for ever: a gate that is always closed tests as
 * little as one that is always open.
 *
 * What is left is the machine itself, which is the honest source anyway. The
 * question these cases ask is whether this machine has the plugin, and the
 * plugin folders are where that is answered.
 *
 * Directed rather than exhaustive, and that is the safety. A full scan loads
 * every bundle installed, which is minutes on a desk with a library on it and
 * one bad plugin away from taking the suite down with it -- which is why the app
 * scans out of process. This walks the folders, which is cheap, and then loads
 * only the bundles whose name matches one a corpus project asked for: three
 * files at most, all of them named in a manifest somebody wrote.
 *
 * Added to the engine's own list rather than to one of its own, because the
 * incumbent leg reads that one and two legs reading two lists are two projects.
 */
void findPluginsFor(const std::vector<std::string>& wanted) {
    // Once per name for the whole run, whether or not it was found. A name that
    // is not on this machine is not on it the second time either, and the walk
    // is the expensive half.
    static std::set<std::string> searched;

    std::vector<std::string> unsearched;
    for (const auto& name : wanted)
        if (searched.insert(name).second)
            unsearched.push_back(name);

    if (unsearched.empty())
        return;

    auto* engine = magda::test::getSharedEngine().getEngine();
    if (engine == nullptr)
        return;

    auto& plugins = engine->getPluginManager();
    auto& formats = plugins.pluginFormatManager;
    auto& known = plugins.knownPluginList;

    // A project names a plugin what its author's host called it, and a bundle is
    // named what its vendor called the file. Those agree often and not always:
    // FabFilter ships "Pro-L 2" inside "FabFilter Pro-L 2.vst3". So the match is
    // containment in either direction, folded for case.
    //
    // Loose on purpose, and it can only be loose in one direction that matters.
    // A false match costs one bundle loaded that resolution then declines,
    // because matchInstalledPlugin has the project's own identifier to check
    // against and this does not; a missed match costs a case that never runs on
    // a machine that could have measured it.
    const auto matches = [&unsearched](const juce::String& identifier) {
        const auto file = juce::File::createFileWithoutCheckingPath(identifier);
        const auto base =
            (file.exists() ? file.getFileNameWithoutExtension() : identifier).toLowerCase();

        return std::any_of(unsearched.begin(), unsearched.end(), [&base](const std::string& name) {
            const auto folded = juce::String(name).toLowerCase();
            return folded.isNotEmpty() && (base.contains(folded) || folded.contains(base));
        });
    };

    for (int index = 0; index < formats.getNumFormats(); ++index) {
        auto* format = formats.getFormat(index);
        if (format == nullptr)
            continue;

        // The walk, which reads directory entries and loads nothing.
        const auto found =
            format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), true);

        for (const auto& identifier : found) {
            if (!matches(identifier))
                continue;

            // And the load, for the handful that matched. This is the step the
            // app runs out of process; in here it is a named bundle rather than
            // a library, and a plugin that cannot survive being asked what it is
            // would not have survived the render either.
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format->findAllTypesForFile(descriptions, identifier);

            for (const auto* description : descriptions)
                known.addType(*description);
        }
    }
}

/// The largest magnitude anywhere in @p buffer, which is all the silence guard
/// below needs: it asks whether a render happened, not how loud it was.
float peakOf(const juce::AudioBuffer<float>& buffer) {
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    return peak;
}

/// Where the artefacts of a failing case go. A parity failure is diagnosed by
/// listening and by looking, and a test that only says "expected 1e-12, got
/// 0.3" makes that the reader's problem.
void writeArtefacts(const std::string& name, const juce::AudioBuffer<float>& native,
                    const juce::AudioBuffer<float>& incumbent, double sampleRate) {
    const auto directory = nullDiffScratchDirectory().getChildFile("failures");
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
bool judgeStretched(const Case& value, const NativeRender& native, const IncumbentRender& incumbent,
                    CaseReport& report, const RunnerLog& log) {
    report.hasSpectral = true;
    report.primingSamples = native.primingSamples;

    // The alignment comes from the envelopes, not from the waveforms. Two
    // vocoders never correlate as waveforms however well aligned they are,
    // so requiring that here would fail every stretched case for being what
    // it was predicted to be. The envelope is what survives the phase
    // difference, and the material these cases play has one to correlate.
    if (report.shiftCorrelationEnvelope < 0.98) {
        log("  " + juce::String(value.name) + ": the envelopes do not correlate (" +
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
        log("  " + juce::String(value.name) + ": no predicted shift is declared, so the measured " +
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
            log("  " + juce::String(value.name) + ": shift " +
                juce::String(report.measuredShift, 1) + " against a predicted " +
                juce::String(value.expectedShiftSamples));
            held = false;
        }
    }

    if (std::abs(report.envelope.lagSamples) > value.envelopeToleranceSamples) {
        log("  " + juce::String(value.name) + ": the envelopes are " +
            juce::String(report.envelope.lagSamples, 2) + " samples apart after the shift");
        held = false;
    }

    if (report.spectra.frames == 0) {
        log("  " + juce::String(value.name) + ": nothing to compare spectrally");
        held = false;
    } else if (value.spectralPercentile95Db > 0.0 &&
               report.spectra.percentile95Db > value.spectralPercentile95Db) {
        log("  " + juce::String(value.name) + ": spectral p95 " +
            juce::String(report.spectra.percentile95Db, 2) + " dB against a bound of " +
            juce::String(value.spectralPercentile95Db, 2));
        held = false;
    }

    return held;
}

}  // namespace

juce::File nullDiffScratchDirectory() {
    auto root =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("magda_null_diff");
    root.createDirectory();
    return root;
}

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

CaseReport runCase(const Case& value, const RunnerLog& log) {
    CaseReport report;
    report.name = value.name;
    report.tier = value.tier;
    report.environment = value.environment();

    // Looked for before the scan is read, so that "this machine has not scanned
    // it" is a fact about the machine rather than about the binary.
    findPluginsFor(externalDevicesIn(value));

    const auto installed = installedPlugins();

    // Asked before either leg is driven, because the answer decides whether
    // there is anything worth driving them for. A project rendered without the
    // plugin it names is a different project, and two legs rendering it without
    // the plugin null perfectly (NullDiffCase.hpp).
    report.absentPlugins = absentPluginsIn(value, installed.knownPlugins);
    if (!report.absentPlugins.empty())
        return report;

    const auto native = renderNative(value, installed);
    const auto incumbent = renderIncumbent(value);

    // What the render ran, whatever the comparison goes on to say. Written
    // before the early returns below so that a case that turns out to be
    // unmeasurable still prints which plugins it had, which is the first thing
    // worth knowing about one that stopped being measurable.
    report.environment.plugins = native.plugins;

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
        options.incumbentNoteEndEarlySamples =
            static_cast<int>(std::llround(value.incumbentNoteEndEarlySeconds * value.sampleRate));

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
                log("  " + juce::String(value.name) + ": track " + juce::String(trackId) +
                    " was captured by " +
                    (nativeStream == native.midiByTrack.end() ? "the incumbent" : "the engine") +
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
                    log("  " + juce::String(value.name) + ": track " + juce::String(trackId) +
                        ": " + problem);
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
    report.measuredShift =
        value.tier == AudioTier::Spectral ? estimate.envelopeSamples : estimate.fractionalSamples;

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
        options.minPeakDb = value.minPeakDb;
        options.asksForTail = value.rendersPastItsMaterial;

        report.hasInvariants = true;
        report.invariants = compareInvariants(native.audio, incumbent.audio, options);
        report.passed = report.invariants.passed() && midiHeld;

        if (!report.invariants.passed()) {
            if (!report.invariants.refusal.empty())
                report.unmeasurable = report.invariants.refusal;
            for (const auto& problem : report.invariants.problems)
                log("  " + juce::String(value.name) + ": " + problem);
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
            report.passed = judgeStretched(value, native, incumbent, report, log);
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

SuiteRun runSuite(const std::vector<Case>& cases, const RunnerLog& log) {
    SuiteRun run;

    // Read per case, so an assertion is attributed to the case that
    // provoked it rather than to the run. Taken once before the walk begins
    // to drop anything logged on the way in, which belongs to whatever ran
    // before this test rather than to its first case.
    auto& watch = magda::test::AssertionWatch::instance();
    watch.take();

    for (const auto& value : cases) {
        const auto started = std::chrono::steady_clock::now();
        auto report = runCase(value, log);
        run.milliseconds.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - started)
                                       .count());

        // An engine that objects to its own graph has not produced a render
        // worth comparing, whatever the residual says. Reported as
        // unmeasurable rather than as a residual, for the same reason a
        // proxy that never arrived is: the number would be about something
        // other than parity, and it would look like a parity failure.
        //
        // Except the ones the case named. The watch is a process-wide hook and it
        // cannot tell whose code raised what, so on a case that loads somebody
        // else's code into this process the premise above -- everything that
        // asserts here is the engine or the harness -- stops being true.
        //
        // Named one at a time rather than waived by the presence of a plugin.
        // "This project hosts a VST3" is not evidence about who asserted: a
        // waiver keyed on it would forgive a genuine engine regression on
        // exactly the three cases worth adding, and would forgive it for a
        // plugin on a bypassed chain that the render never instantiated.
        // Retrospect hands JUCE its own URI where a path is expected, once per
        // instantiation, and that one sentence is what the case declares.
        //
        // Recorded and printed either way, whichever side it falls on. Nothing
        // is dropped; what changes is what it is allowed to conclude.
        if (auto fired = watch.take(); !fired.empty()) {
            auto unexplained = std::vector<juce::String>{};

            for (const auto& assertion : fired) {
                const auto named = std::any_of(
                    value.expectedHostedAssertions.begin(), value.expectedHostedAssertions.end(),
                    [&assertion](const std::string& expected) {
                        return assertion.toStdString().find(expected) != std::string::npos;
                    });

                log("  " + juce::String(value.name) + (named ? " (hosted): " : ": ") + assertion);

                if (named)
                    report.hostedAssertions.push_back(assertion.toStdString());
                else
                    unexplained.push_back(assertion);
            }

            if (!unexplained.empty()) {
                report.passed = false;
                report.unmeasurable =
                    "the engine asserted while rendering: " + unexplained.front().toStdString() +
                    (unexplained.size() > 1
                         ? " (and " + std::to_string(unexplained.size() - 1) + " more)"
                         : "");
                run.asserted.insert(value.name);
            }
        }

        // And the other direction, the same rule the declared diagnostics live
        // by: a plugin that stops asserting where it did has to come off the
        // case, or the declaration sits there forgiving something that no
        // longer happens.
        if (!value.expectedHostedAssertions.empty() && report.absentPlugins.empty()) {
            for (const auto& expected : value.expectedHostedAssertions) {
                const auto seen =
                    std::any_of(report.hostedAssertions.begin(), report.hostedAssertions.end(),
                                [&expected](const std::string& assertion) {
                                    return assertion.find(expected) != std::string::npos;
                                });

                if (!seen) {
                    report.passed = false;
                    report.unmeasurable = "the case expects the hosted plugin to assert \"" +
                                          expected + "\", and it did not";
                }
            }
        }

        // A case that never ran is neither failing nor unmeasurable. Both of
        // those are complaints -- one says the engines disagree, the other says
        // the harness could not tell -- and an absent plugin is neither: it is a
        // fact about which plugins are on this machine, and the machine every
        // release is built on has none of them.
        if (!report.absentPlugins.empty()) {
            run.notRun.insert(report.name);
        } else {
            if (!report.passed)
                run.failing.insert(report.name);
            if (!report.unmeasurable.empty())
                run.unmeasurable.insert(report.name);
        }

        run.reports.push_back(std::move(report));
    }

    return run;
}

}  // namespace magda::nulldiff
