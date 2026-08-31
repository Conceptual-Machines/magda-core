#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "NullDiffCompare.hpp"
#include "NullDiffMaterial.hpp"

/**
 * The comparator behind the null-diff corpus (#2040), against known-bad pairs.
 *
 * This is the one piece of that corpus nothing else checks. Every other test in
 * the slice asserts through it, so a comparator that quietly passes everything
 * would report parity it never measured, which is a worse outcome than an
 * engine that fails. Hence: it is handed pairs that are wrong in each of the
 * ways an engine can be wrong, and it has to say so.
 */

using namespace magda::nulldiff;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kBpm = 120.0;

juce::AudioBuffer<float> tone(double durationSeconds, double frequency = 220.0) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Tone;
    spec.sampleRate = kSampleRate;
    spec.durationSeconds = durationSeconds;
    spec.frequency = frequency;
    return renderMaterial(spec);
}

juce::AudioBuffer<float> impulses(double durationSeconds, double intervalSeconds = 0.25) {
    MaterialSpec spec;
    spec.kind = MaterialKind::Impulses;
    spec.sampleRate = kSampleRate;
    spec.durationSeconds = durationSeconds;
    spec.intervalSeconds = intervalSeconds;
    return renderMaterial(spec);
}

/// The same material, later by @p samples, in a buffer of the same length.
juce::AudioBuffer<float> delayed(const juce::AudioBuffer<float>& source, int samples) {
    juce::AudioBuffer<float> result(source.getNumChannels(), source.getNumSamples());
    result.clear();
    for (auto channel = 0; channel < source.getNumChannels(); ++channel)
        for (auto sample = samples; sample < source.getNumSamples(); ++sample)
            result.setSample(channel, sample, source.getSample(channel, sample - samples));
    return result;
}

/// Every sample scaled, which is a residual with a known peak.
juce::AudioBuffer<float> scaled(const juce::AudioBuffer<float>& source, float gain) {
    juce::AudioBuffer<float> result(source);
    result.applyGain(gain);
    return result;
}

std::int64_t samplesAt(double seconds) {
    return static_cast<std::int64_t>(std::llround(seconds * kSampleRate));
}

MidiEvent noteOn(int channel, int pitch, int velocity, double seconds) {
    return {samplesAt(seconds), static_cast<std::uint8_t>(0x90 | (channel - 1)),
            static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity)};
}

MidiEvent noteOff(int channel, int pitch, double seconds) {
    return {samplesAt(seconds), static_cast<std::uint8_t>(0x80 | (channel - 1)),
            static_cast<std::uint8_t>(pitch), 0};
}

MidiEvent controller(int channel, int number, int value, double seconds) {
    return {samplesAt(seconds), static_cast<std::uint8_t>(0xB0 | (channel - 1)),
            static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value)};
}

/// One note, so that a controller test is not also a note test.
MidiStream withNote(MidiStream stream) {
    stream.push_back(noteOn(1, 60, 100, 0.0));
    stream.push_back(noteOff(1, 60, 1.0));
    return stream;
}

/// A ramp from 0 to 127 over @p durationSeconds, emitted the way the engine
/// does: one message per change of the quantised value, and nothing in between.
MidiStream curveByValueChange(double durationSeconds, int number = 1) {
    MidiStream stream;
    for (auto value = 0; value <= 127; ++value)
        stream.push_back(
            controller(1, number, value, durationSeconds * static_cast<double>(value) / 127.0));
    return stream;
}

/// The same ramp emitted the way the fork does: the value the curve is at, on a
/// 1/16-beat grid, whether it moved or not.
MidiStream curveOnGrid(double durationSeconds, int number = 1) {
    MidiStream stream;
    const auto step = 60.0 / kBpm / 16.0;
    for (auto time = 0.0; time <= durationSeconds; time += step) {
        const auto value = static_cast<int>(std::floor(127.0 * time / durationSeconds));
        stream.push_back(controller(1, number, std::min(127, value), time));
    }
    return stream;
}

/// What went wrong, for a failure message. Empty is a passing comparison.
std::string firstProblem(const MidiComparison& result) {
    return result.problems.empty() ? std::string{} : result.problems.front();
}

MidiCompareOptions midiOptions() {
    MidiCompareOptions options;
    options.sampleRate = kSampleRate;
    options.bpm = kBpm;
    return options;
}

}  // namespace

// =============================================================================
// Audio
// =============================================================================

TEST_CASE("A pair that differs by one sample of placement is reported at full scale",
          "[nulldiff][compare]") {
    // The failure the corpus exists to catch, and the one a sloppy comparator
    // misses by aligning it away.
    const auto native = impulses(2.0);
    const auto incumbent = delayed(native, 1);

    const auto result = compareAudio(native, incumbent);

    CHECK_FALSE(result.withinFloor());
    CHECK(result.peakDb > -1.0);
    CHECK(result.firstDivergence >= 0);

    // The first impulse is at sample 0 in one and sample 1 in the other, so
    // that is where they part company.
    CHECK(result.firstDivergence == 0);
}

TEST_CASE("Identical renders null", "[nulldiff][compare]") {
    const auto native = impulses(2.0);

    const auto result = compareAudio(native, native);

    CHECK(result.withinFloor());
    CHECK(result.firstDivergence == -1);
    CHECK(result.shiftSamples == 0);
    CHECK(result.comparedSamples == native.getNumSamples());
}

TEST_CASE("The floor is checked either side of itself rather than at it", "[nulldiff][compare]") {
    // A residual of a known size, made by scaling one leg: peak residual is the
    // material's peak times the gain difference.
    const auto native = tone(1.0);
    const auto peak = native.getMagnitude(0, native.getNumSamples());

    SECTION("under the floor passes") {
        const auto quiet = static_cast<float>(std::pow(10.0, -130.0 / 20.0)) / peak;
        const auto result = compareAudio(native, scaled(native, 1.0f - quiet));
        CHECK(result.withinFloor());
    }

    SECTION("over the floor fails") {
        const auto loud = static_cast<float>(std::pow(10.0, -110.0 / 20.0)) / peak;
        const auto result = compareAudio(native, scaled(native, 1.0f - loud));
        CHECK_FALSE(result.withinFloor());
        CHECK(result.peakDb > -120.0);
    }
}

TEST_CASE("A refusal is never reported as a residual", "[nulldiff][compare]") {
    const auto mono = tone(1.0);
    juce::AudioBuffer<float> stereo(2, mono.getNumSamples());
    stereo.clear();

    const auto result = compareAudio(mono, stereo);

    CHECK_FALSE(result.refusal.empty());
    CHECK(result.comparedSamples == 0);
}

TEST_CASE("The shift search finds a known shift", "[nulldiff][compare][shift]") {
    const auto native = tone(2.0);
    const auto incumbent = delayed(native, 1024);

    const auto estimate = estimateShift(native, incumbent, 4096);

    REQUIRE(estimate.found);
    CHECK(estimate.samples == 1024);

    AudioCompareOptions options;
    options.measureShift = true;
    const auto result = compareAudio(native, incumbent, options);

    CHECK(result.shiftSamples == 1024);
    CHECK(result.withinFloor());
}

TEST_CASE("Two identical renders are not offset from each other", "[nulldiff][compare][shift]") {
    // The offset is measured on every audio case and printed whether or not it
    // is applied, so a number here that is not zero reads, on a case that nulled
    // bit for bit, as a case that is thirty samples out. Periodic material is
    // where a search can talk itself into one: a square wave correlates with
    // itself at more lags than nature intended.
    MaterialSpec spec;
    spec.kind = MaterialKind::Steps;
    spec.sampleRate = kSampleRate;
    spec.durationSeconds = 2.0;
    spec.intervalSeconds = 0.5;
    const auto square = renderMaterial(spec);

    const auto estimate = estimateShift(square, square, 256);

    CHECK(estimate.samples == 0);
    CHECK(std::abs(estimate.fractionalSamples) < 1.0e-6);
}

TEST_CASE("The shift search declines a pair that differs in content",
          "[nulldiff][compare][shift]") {
    // A comparator that slides until something matches will always find
    // something, and what it finds here would make the case pass.
    const auto native = tone(2.0, 220.0);

    MaterialSpec spec;
    spec.kind = MaterialKind::Noise;
    spec.sampleRate = kSampleRate;
    spec.durationSeconds = 2.0;
    const auto incumbent = renderMaterial(spec);

    const auto estimate = estimateShift(native, incumbent, 4096);

    CHECK_FALSE(estimate.found);
    CHECK(estimate.samples == 0);

    AudioCompareOptions options;
    options.measureShift = true;
    const auto result = compareAudio(native, incumbent, options);

    CHECK(result.shiftNotFound);
    CHECK_FALSE(result.withinFloor());
}

TEST_CASE("One shift per case, measured at the start", "[nulldiff][compare][shift]") {
    // A pair aligned at the start and drifting later has to fail, at the drift,
    // rather than be re-aligned region by region until it passes. This is what
    // the auto-tempo case leans on: the fork's stretcher priming is measured
    // once, and a lateness that grows when the ratio moves is a finding.
    const auto native = tone(4.0);

    auto incumbent = delayed(native, 512);
    const auto halfway = incumbent.getNumSamples() / 2;
    for (auto channel = 0; channel < incumbent.getNumChannels(); ++channel)
        for (auto sample = halfway; sample < incumbent.getNumSamples(); ++sample)
            incumbent.setSample(channel, sample,
                                native.getSample(channel, juce::jmax(0, sample - 1024)));

    AudioCompareOptions options;
    options.measureShift = true;
    const auto result = compareAudio(native, incumbent, options);

    CHECK(result.shiftSamples == 512);
    CHECK_FALSE(result.withinFloor());
    CHECK(result.firstDivergence >= halfway - 1024);
}

// =============================================================================
// MIDI notes
// =============================================================================

TEST_CASE("Matching note streams pass", "[nulldiff][compare][midi]") {
    MidiStream stream;
    stream.push_back(noteOn(1, 60, 100, 0.0));
    stream.push_back(noteOff(1, 60, 0.5));
    stream.push_back(noteOn(1, 64, 90, 0.5));
    stream.push_back(noteOff(1, 64, 1.0));

    const auto result = compareMidi(stream, stream, midiOptions());

    CHECK(result.notesMatch);
    CHECK(result.notesCompared == 2);
    CHECK(result.problems.empty());
}

TEST_CASE("Every way a note can differ is reported", "[nulldiff][compare][midi]") {
    MidiStream native;
    native.push_back(noteOn(1, 60, 100, 0.0));
    native.push_back(noteOff(1, 60, 0.5));

    SECTION("a missing note") {
        const auto result = compareMidi(native, {}, midiOptions());
        CHECK_FALSE(result.notesMatch);
        CHECK(result.notesOnlyInNative == 1);
    }

    SECTION("an extra note") {
        auto incumbent = native;
        incumbent.push_back(noteOn(1, 67, 100, 0.75));
        incumbent.push_back(noteOff(1, 67, 1.0));

        const auto result = compareMidi(native, incumbent, midiOptions());
        CHECK_FALSE(result.notesMatch);
        CHECK(result.notesOnlyInIncumbent == 1);
    }

    SECTION("a wrong velocity") {
        MidiStream incumbent;
        incumbent.push_back(noteOn(1, 60, 99, 0.0));
        incumbent.push_back(noteOff(1, 60, 0.5));

        const auto result = compareMidi(native, incumbent, midiOptions());
        CHECK_FALSE(result.notesMatch);
        CHECK(result.notesMismatched == 1);
    }

    SECTION("a wrong channel") {
        // Compared as assigned rather than canonicalised to order of first use:
        // the fork's MPE round-robin was ported deliberately, so a channel that
        // differs is a finding.
        MidiStream incumbent;
        incumbent.push_back(noteOn(2, 60, 100, 0.0));
        incumbent.push_back(noteOff(2, 60, 0.5));

        const auto result = compareMidi(native, incumbent, midiOptions());
        CHECK_FALSE(result.notesMatch);
        CHECK(result.notesOnlyInNative == 1);
        CHECK(result.notesOnlyInIncumbent == 1);
    }

    SECTION("a wrong length") {
        MidiStream incumbent;
        incumbent.push_back(noteOn(1, 60, 100, 0.0));
        incumbent.push_back(noteOff(1, 60, 0.6));

        const auto result = compareMidi(native, incumbent, midiOptions());
        CHECK_FALSE(result.notesMatch);
        CHECK(result.notesMismatched == 1);
    }
}

TEST_CASE("A note within the rounding allowance is not a difference", "[nulldiff][compare][midi]") {
    // Two engines round a beat to a sample through different arithmetic, and
    // one sample of that is not a finding. Two is.
    MidiStream native;
    native.push_back(noteOn(1, 60, 100, 0.0));
    native.push_back(noteOff(1, 60, 0.5));

    auto shifted = [](std::int64_t samples) {
        MidiStream stream;
        stream.push_back({samples, 0x90, 60, 100});
        stream.push_back({samplesAt(0.5) + samples, 0x80, 60, 0});
        return stream;
    };

    CHECK(compareMidi(native, shifted(1), midiOptions()).notesMatch);
    CHECK_FALSE(compareMidi(native, shifted(2), midiOptions()).notesMatch);
}

TEST_CASE("A declared note shift is applied and nothing else is", "[nulldiff][compare][midi]") {
    // The fork drops midiOffset on an unlooped arranger clip, so every note of
    // such a clip lands offset. Declared by the case, asserted here, and a note
    // that moves for any other reason still breaks.
    MidiStream native;
    native.push_back(noteOn(1, 60, 100, 1.0));
    native.push_back(noteOff(1, 60, 1.5));

    MidiStream incumbent;
    incumbent.push_back(noteOn(1, 60, 100, 1.25));
    incumbent.push_back(noteOff(1, 60, 1.75));

    auto options = midiOptions();
    options.noteShiftSamples = samplesAt(0.25);
    CHECK(compareMidi(native, incumbent, options).notesMatch);

    options.noteShiftSamples = samplesAt(0.5);
    CHECK_FALSE(compareMidi(native, incumbent, options).notesMatch);
}

TEST_CASE("A hanging note fails whichever engine left it", "[nulldiff][compare][midi]") {
    MidiStream hanging;
    hanging.push_back(noteOn(1, 60, 100, 0.0));

    MidiStream complete;
    complete.push_back(noteOn(1, 60, 100, 0.0));
    complete.push_back(noteOff(1, 60, 0.5));

    SECTION("the native render") {
        const auto result = compareMidi(hanging, complete, midiOptions());
        CHECK(result.nativeHanging == 1);
        CHECK_FALSE(result.notesMatch);
    }

    SECTION("the incumbent") {
        const auto result = compareMidi(complete, hanging, midiOptions());
        CHECK(result.incumbentHanging == 1);
        CHECK_FALSE(result.notesMatch);
    }
}

TEST_CASE("A note-off for a note that never started is reported", "[nulldiff][compare][midi]") {
    MidiStream orphan;
    orphan.push_back(noteOff(1, 60, 0.5));

    const auto lifetime = pairNotes(orphan);

    CHECK(lifetime.unmatchedOffs == 1);
    CHECK(lifetime.notes.empty());
    CHECK_FALSE(compareMidi(orphan, {}, midiOptions()).notesMatch);
}

TEST_CASE("A note-on of velocity zero ends a note", "[nulldiff][compare][midi]") {
    MidiStream stream;
    stream.push_back(noteOn(1, 60, 100, 0.0));
    stream.push_back(noteOn(1, 60, 0, 0.5));

    const auto lifetime = pairNotes(stream);

    REQUIRE(lifetime.notes.size() == 1);
    CHECK(lifetime.hanging == 0);
    CHECK(lifetime.notes.front().offSample == samplesAt(0.5));
}

// =============================================================================
// MIDI controllers, which is the interesting half
// =============================================================================

TEST_CASE("A dense value-change stream and a sparse grid stream carry the same curve",
          "[nulldiff][compare][midi][controllers]") {
    // The recorded divergence, and the reason controllers are compared as a
    // curve against a sampling of it rather than list against list.
    const auto native = withNote(curveByValueChange(4.0));
    const auto incumbent = withNote(curveOnGrid(4.0));

    const auto result = compareMidi(native, incumbent, midiOptions());

    INFO(firstProblem(result));
    CHECK(result.controllersMatch);
    CHECK(result.notesMatch);
}

TEST_CASE("A fast curve the fork can only sample three times still matches",
          "[nulldiff][compare][midi][controllers]") {
    // A hundred milliseconds is four grid points there and about a hundred
    // here. Comparing the two functions instant for instant would fail this,
    // which is why that is not the test.
    const auto native = withNote(curveByValueChange(0.1));
    const auto incumbent = withNote(curveOnGrid(0.1));

    // And the fork's last grid point lands before the curve arrives, so it
    // never sends the value at the end of the sweep at all. Missing the extreme
    // of something moving faster than the grid IS the divergence, so it is the
    // one thing about these two streams that is deliberately not asserted.
    const auto reach = [](const MidiStream& stream) {
        auto highest = 0;
        for (const auto& event : stream)
            if (event.type() == 0xB0)
                highest = std::max(highest, static_cast<int>(event.data2));
        return highest;
    };
    REQUIRE(reach(native) == 127);
    REQUIRE(reach(incumbent) < 127);

    const auto result = compareMidi(native, incumbent, midiOptions());

    INFO(firstProblem(result));
    CHECK(result.controllersMatch);
}

TEST_CASE("A value two units out is reported", "[nulldiff][compare][midi][controllers]") {
    const auto native = withNote(curveByValueChange(4.0));

    auto incumbent = withNote(curveOnGrid(4.0));
    for (auto& event : incumbent)
        if (event.type() == 0xB0 && event.sample > samplesAt(2.0))
            event.data2 = static_cast<std::uint8_t>(std::min(127, event.data2 + 2));

    const auto result = compareMidi(native, incumbent, midiOptions());

    CHECK_FALSE(result.controllersMatch);
    REQUIRE_FALSE(result.problems.empty());
}

TEST_CASE("The right values arriving a beat late are reported",
          "[nulldiff][compare][midi][controllers]") {
    const auto native = withNote(curveByValueChange(4.0));

    auto incumbent = withNote(curveOnGrid(4.0));
    for (auto& event : incumbent)
        if (event.type() == 0xB0)
            event.sample += samplesAt(0.5);

    const auto result = compareMidi(native, incumbent, midiOptions());

    CHECK_FALSE(result.controllersMatch);
}

TEST_CASE("A controller that stops halfway is reported", "[nulldiff][compare][midi][controllers]") {
    const auto native = withNote(curveByValueChange(4.0));

    MidiStream incumbent;
    for (const auto& event : withNote(curveOnGrid(4.0)))
        if (event.type() != 0xB0 || event.sample <= samplesAt(2.0))
            incumbent.push_back(event);

    const auto result = compareMidi(native, incumbent, midiOptions());

    CHECK_FALSE(result.controllersMatch);
}

TEST_CASE("A curve that flattens is not a shorter stream",
          "[nulldiff][compare][midi][controllers]") {
    // The fork keeps emitting on its grid after the curve stops moving. Those
    // repeats say nothing, and counting them would make its span look longer
    // than the engine's on every curve that ends flat.
    auto native = withNote(curveByValueChange(2.0));

    auto incumbent = withNote(curveOnGrid(2.0));
    for (auto time = 2.0; time < 4.0; time += 60.0 / kBpm / 16.0)
        incumbent.push_back(controller(1, 1, 127, time));

    const auto result = compareMidi(native, incumbent, midiOptions());

    INFO(firstProblem(result));
    CHECK(result.controllersMatch);
}

TEST_CASE("A controller only one engine sends is reported",
          "[nulldiff][compare][midi][controllers]") {
    const auto native = withNote(curveByValueChange(4.0, 1));
    const auto incumbent = withNote(curveOnGrid(4.0, 74));

    const auto result = compareMidi(native, incumbent, midiOptions());

    CHECK_FALSE(result.controllersMatch);
    CHECK(result.problems.size() >= 2);
}

TEST_CASE("Pitch bend is compared at its own resolution",
          "[nulldiff][compare][midi][controllers]") {
    const auto bend = [](int value, double seconds) {
        return MidiEvent{samplesAt(seconds), 0xE0, static_cast<std::uint8_t>(value & 0x7F),
                         static_cast<std::uint8_t>((value >> 7) & 0x7F)};
    };

    MidiStream native;
    MidiStream incumbent;
    for (auto step = 0; step <= 64; ++step) {
        const auto time = 2.0 * static_cast<double>(step) / 64.0;
        native.push_back(bend(8192 + step * 64, time));
        incumbent.push_back(bend(8192 + step * 64, time));
    }

    CHECK(compareMidi(native, incumbent, midiOptions()).controllersMatch);

    // Fourteen bits, so two units apart is two units apart here as well.
    incumbent.back().data1 = static_cast<std::uint8_t>((8192 + 64 * 64 + 8) & 0x7F);
    CHECK_FALSE(compareMidi(native, incumbent, midiOptions()).controllersMatch);
}

// =============================================================================
// The report
// =============================================================================

namespace {

CaseEnvironment standardEnvironment() {
    CaseEnvironment environment;
    environment.sampleRate = kSampleRate;
    environment.blockSize = 512;
    return environment;
}

}  // namespace

TEST_CASE("The report is canonical", "[nulldiff][compare][report]") {
    CaseReport first;
    first.name = "placement.grid";
    first.tier = AudioTier::Exact;
    first.environment = standardEnvironment();
    first.hasAudio = true;
    first.audio = compareAudio(impulses(0.5), impulses(0.5));
    first.passed = true;

    CaseReport second;
    second.name = "midi.notes";
    second.tier = AudioTier::None;
    second.environment = standardEnvironment();
    second.hasMidi = true;
    second.midi.notesMatch = true;
    second.midi.controllersMatch = true;
    second.midi.notesCompared = 8;
    second.passed = true;

    const auto once = formatReport({first, second}, standardEnvironment());
    const auto again = formatReport({first, second}, standardEnvironment());

    CHECK(once == again);
    CHECK(once.find("magda-null-diff v2") != std::string::npos);
    CHECK(once.find("placement.grid") != std::string::npos);
    CHECK(once.find("notes=8") != std::string::npos);
}

TEST_CASE("A case that rendered somewhere else says so", "[nulldiff][compare][report]") {
    // The failure this prevents: a residual measured at another rate, another
    // block size or another seed read as a residual measured here. Without the
    // environment beside it, a number about a different render is
    // indistinguishable from a number about this one.
    CaseReport standard;
    standard.name = "placement.grid";
    standard.environment = standardEnvironment();
    standard.hasAudio = true;
    standard.passed = true;

    CaseReport elsewhere;
    elsewhere.name = "rate.96k";
    elsewhere.environment = standardEnvironment();
    elsewhere.environment.sampleRate = 96000.0;
    elsewhere.hasAudio = true;
    elsewhere.passed = true;

    const auto text = formatReport({standard, elsewhere}, standardEnvironment());

    // On the deviating case's line, and on no other. Read line by line rather
    // than by offset, because "the environment appears somewhere after this
    // name" is true of every line that follows it.
    std::vector<std::string> lines;
    for (std::size_t begin = 0; begin < text.size();) {
        const auto end = text.find('\n', begin);
        lines.push_back(text.substr(begin, end - begin));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }

    auto standardLines = 0;
    auto deviatingLines = 0;
    for (const auto& line : lines) {
        if (line.find("placement.grid") != std::string::npos) {
            ++standardLines;
            CHECK(line.find("[rate=") == std::string::npos);
        }
        if (line.find("rate.96k") != std::string::npos) {
            ++deviatingLines;
            CHECK(line.find("[rate=96000") != std::string::npos);
        }
    }

    CHECK(standardLines == 1);
    CHECK(deviatingLines == 1);
}

TEST_CASE("A case that asserts both prints both", "[nulldiff][compare][report]") {
    // The point of separating the audio tier from the MIDI comparison is that a
    // project can do both. An exclusive chain would print the notes and drop the
    // residual, so the number that moves would go missing from exactly the mixed
    // cases the separation exists to allow.
    CaseReport both;
    both.name = "project.instrument";
    both.tier = AudioTier::Exact;
    both.environment = standardEnvironment();
    both.hasAudio = true;
    both.audio = compareAudio(impulses(0.5), impulses(0.5));
    both.hasMidi = true;
    both.midi.notesMatch = true;
    both.midi.controllersMatch = true;
    both.midi.notesCompared = 12;
    both.passed = true;

    const auto text = formatReport({both}, standardEnvironment());

    CHECK(text.find("peak=") != std::string::npos);
    CHECK(text.find("notes=12") != std::string::npos);
}

TEST_CASE("An unmeasurable case is not a residual", "[nulldiff][compare][report]") {
    // A proxy that never arrived reported as a parity failure costs somebody a
    // day inside the engine looking for a bug that is not there.
    CaseReport report;
    report.name = "stretch.signalsmith";
    report.tier = AudioTier::Spectral;
    report.environment = standardEnvironment();
    report.unmeasurable = "proxy not ready";
    report.passed = false;

    const auto text = formatReport({report}, standardEnvironment());

    CHECK(text.find("unmeasurable: proxy not ready") != std::string::npos);
    CHECK(text.find("peak=") == std::string::npos);
}

// =============================================================================
// The invariants tier
// =============================================================================

namespace {

InvariantOptions invariantOptions(double maxStep = 0.5) {
    InvariantOptions options;
    options.sampleRate = kSampleRate;
    options.maxStepPerSample = maxStep;
    options.minPeakDb = -40.0;
    options.tailSeconds = 0.05;
    options.tailFloorDb = -60.0;
    return options;
}

/// A tone that fades out, which is what a well-behaved render looks like to
/// this tier: continuous throughout and silent by the end.
juce::AudioBuffer<float> decayingTone(double frequency = 220.0, int samples = 8192,
                                      double phase = 0.0) {
    juce::AudioBuffer<float> buffer(1, samples);
    auto* data = buffer.getWritePointer(0);
    for (auto index = 0; index < samples; ++index) {
        const auto position = static_cast<double>(index) / static_cast<double>(samples);
        const auto envelope = std::exp(-16.0 * position);
        data[index] =
            static_cast<float>(0.5 * envelope *
                               std::sin(2.0 * juce::MathConstants<double>::pi * frequency *
                                            static_cast<double>(index) / kSampleRate +
                                        phase));
    }
    return buffer;
}

}  // namespace

TEST_CASE("A pair that will never null can still be judged", "[nulldiff][compare][invariants]") {
    // The case this tier exists for: two renders of the same material that
    // differ in phase, which is what a plugin holding its own state between
    // calls produces. A residual comparison reports full scale; the invariants
    // report that both renders are well formed, which is the true answer.
    const auto native = decayingTone();
    const auto incumbent = decayingTone(220.0, 8192, 0.9);

    CHECK_FALSE(compareAudio(native, incumbent).nulled());

    const auto invariants = compareInvariants(native, incumbent, invariantOptions());
    CHECK(invariants.passed());
    CHECK(invariants.problems.empty());
}

TEST_CASE("The invariants tier refuses a case that declared no bound",
          "[nulldiff][compare][invariants]") {
    // A bound nobody declared would certify a check that never ran, and this
    // tier has no residual to fall back on. Refusing is the only honest answer.
    const auto tone = decayingTone();

    const auto invariants = compareInvariants(tone, tone, invariantOptions(0.0));

    CHECK_FALSE(invariants.passed());
    CHECK(invariants.refusal == "no discontinuity bound declared");
}

TEST_CASE("The invariants tier refuses a case that declared no liveness floor",
          "[nulldiff][compare][invariants]") {
    // The same rule as the step bound, and it needs stating separately because
    // the two catch different things. This tier computes no residual, so nothing
    // else in it would notice a leg going silent.
    const auto tone = decayingTone();

    auto options = invariantOptions();
    options.minPeakDb = std::numeric_limits<double>::infinity();

    const auto invariants = compareInvariants(tone, tone, options);

    CHECK_FALSE(invariants.passed());
    CHECK(invariants.refusal == "no liveness floor declared");
}

TEST_CASE("One silent render is never certified", "[nulldiff][compare][invariants]") {
    // The hole a waived tail leaves, and the reason liveness is asked per side.
    // Silence is finite, it is the same length as anything, it steps by nothing
    // and it has decayed by the end: every other check in this tier passes on
    // it. A case that renders eight beats of a long arrangement waives the tail,
    // so without this a native leg that stopped rendering would report ok
    // against an incumbent that sounded (#2175).
    // Cleared explicitly: JUCE's constructor leaves the buffer as it found the
    // memory, and a test for silence built out of whatever was on the heap is a
    // test for whatever was on the heap.
    juce::AudioBuffer<float> silence(1, 8192);
    silence.clear();

    auto options = invariantOptions();
    options.asksForTail = false;

    const auto invariants = compareInvariants(silence, decayingTone(), options);

    // Everything else it could be asked, it answers yes to.
    CHECK(invariants.finite);
    CHECK(invariants.lengthsMatch);
    CHECK(invariants.continuous);

    CHECK_FALSE(invariants.bothSound);
    CHECK_FALSE(invariants.passed());
    REQUIRE_FALSE(invariants.problems.empty());
    CHECK(invariants.problems.front().find("did not sound") != std::string::npos);

    // And the mirror, so the check cannot be one-sided.
    const auto other = compareInvariants(decayingTone(), silence, options);
    CHECK_FALSE(other.bothSound);
    CHECK_FALSE(other.passed());
}

TEST_CASE("A waived tail is never reported as a tail that held",
          "[nulldiff][compare][invariants]") {
    // A case whose render window ends before its material does cannot be asked
    // what is left running afterwards. What it must not do is answer.
    juce::AudioBuffer<float> ringing(1, 8192);
    auto* data = ringing.getWritePointer(0);
    for (auto index = 0; index < ringing.getNumSamples(); ++index)
        data[index] =
            static_cast<float>(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 *
                                              static_cast<double>(index) / kSampleRate));

    auto options = invariantOptions();
    options.asksForTail = false;

    const auto invariants = compareInvariants(ringing, ringing, options);

    CHECK(invariants.passed());
    CHECK_FALSE(invariants.tailAsked);

    // Measured and carried even so: the number is still there to read, it is
    // just not a verdict.
    CHECK_FALSE(invariants.tailDecays);
    CHECK(invariants.tailPeakNativeDb > -60.0);
    CHECK(invariants.problems.empty());
}

TEST_CASE("A render that is not finite is never certified", "[nulldiff][compare][invariants]") {
    auto native = decayingTone();
    native.getWritePointer(0)[1000] = std::numeric_limits<float>::quiet_NaN();

    const auto invariants = compareInvariants(native, decayingTone(), invariantOptions());

    CHECK_FALSE(invariants.finite);
    CHECK_FALSE(invariants.passed());
    REQUIRE_FALSE(invariants.problems.empty());
    CHECK(invariants.problems.front().find("not finite") != std::string::npos);
}

TEST_CASE("A click past the bound is reported", "[nulldiff][compare][invariants]") {
    // The failure that survives an engine agreeing with itself: something swapped
    // without a ramp, which is a step in one render and audible as a click.
    auto native = decayingTone();
    native.getWritePointer(0)[2000] = 0.9f;
    native.getWritePointer(0)[2001] = -0.9f;

    const auto invariants = compareInvariants(native, decayingTone(), invariantOptions());

    CHECK(invariants.finite);
    CHECK_FALSE(invariants.continuous);
    CHECK(invariants.worstStepNative > 1.0);
    CHECK(invariants.worstStepAt == 2001);
    CHECK_FALSE(invariants.passed());
}

TEST_CASE("A tail that never decays is reported", "[nulldiff][compare][invariants]") {
    // A device left running past the end of its material. Both sides are asked
    // on their own, because two engines leaking alike would cancel in a residual
    // and be reported as agreement.
    juce::AudioBuffer<float> ringing(1, 8192);
    auto* data = ringing.getWritePointer(0);
    for (auto index = 0; index < ringing.getNumSamples(); ++index)
        data[index] =
            static_cast<float>(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 *
                                              static_cast<double>(index) / kSampleRate));

    const auto invariants = compareInvariants(ringing, ringing, invariantOptions());

    CHECK(invariants.continuous);
    CHECK_FALSE(invariants.tailDecays);
    CHECK_FALSE(invariants.passed());
    CHECK(invariants.tailPeakNativeDb > -60.0);
}

TEST_CASE("Renders of different lengths are reported rather than trimmed",
          "[nulldiff][compare][invariants]") {
    const auto native = decayingTone(220.0, 8192);
    const auto incumbent = decayingTone(220.0, 8000);

    const auto invariants = compareInvariants(native, incumbent, invariantOptions());

    CHECK_FALSE(invariants.lengthsMatch);
    CHECK_FALSE(invariants.passed());
}

// =============================================================================
// Sub-sample alignment, and the stretched-case assertions
// =============================================================================

TEST_CASE("The shift search resolves below a sample", "[nulldiff][compare][shift]") {
    // A whole-sample answer is not enough for band-limited material: at 220 Hz
    // one sample of misalignment is a residual around -30 dB, so a case can be
    // a fraction of a sample out and look like a difference in the material
    // rather than what it is, which is a difference in the timing.
    const auto native = tone(2.0);

    for (const auto delay : {0.25, 0.5, 1.75, 3.5}) {
        const auto incumbent = delayFractionally(native, delay);
        const auto estimate = estimateShift(native, incumbent, 512);

        INFO("delay " << delay << " measured " << estimate.fractionalSamples);
        REQUIRE(estimate.found);
        CHECK(std::abs(estimate.fractionalSamples - delay) < 0.05);
    }
}

TEST_CASE("A fractional delay is undone by aligning fractionally", "[nulldiff][compare][shift]") {
    // What a case does once the offset has a mechanism: align by it, then
    // require the null at the floor rather than accepting the residual.
    const auto native = tone(2.0);
    const auto incumbent = delayFractionally(native, 0.6);

    const auto before = compareAudio(native, incumbent);
    CHECK_FALSE(before.withinFloor());

    const auto aligned = delayFractionally(native, 0.6);
    const auto after = compareAudio(aligned, incumbent);

    INFO("peak " << formatDb(after.peakDb));
    CHECK(after.peakDb < -100.0);
}

TEST_CASE("Envelope timing catches a placement error the spectrum would not",
          "[nulldiff][compare][envelope]") {
    // The assertion that keeps a placement bug visible once waveforms cannot be
    // compared. Two bursts of the same tone, one later than the other: their
    // magnitude spectra are near enough identical and their envelopes are not.
    const auto burst = [](double startSeconds) {
        MaterialSpec spec;
        spec.kind = MaterialKind::Tone;
        spec.sampleRate = kSampleRate;
        spec.durationSeconds = 0.5;
        auto tone = renderMaterial(spec);

        juce::AudioBuffer<float> out(1, static_cast<int>(kSampleRate * 2.0));
        out.clear();
        const auto at = static_cast<int>(startSeconds * kSampleRate);
        out.copyFrom(0, at, tone, 0, 0, tone.getNumSamples());
        return out;
    };

    const auto native = burst(0.5);

    SECTION("aligned") {
        const auto agreement = compareEnvelopes(native, native, 0, kSampleRate);
        CHECK(std::abs(agreement.lagSamples) <= 1.0);
        CHECK(agreement.correlation > 0.99);
    }

    SECTION("a burst in the wrong place") {
        const auto agreement = compareEnvelopes(native, burst(0.52), 0, kSampleRate);
        CHECK(std::abs(agreement.lagSamples) > 1.0);
    }
}

TEST_CASE("Magnitude agrees where phase does not", "[nulldiff][compare][spectral]") {
    // The claim a stretched case makes. Two signals with the same magnitude
    // spectrum and different phase are the same sound, and that is exactly the
    // pair two vocoders produce.
    const auto native = tone(2.0, 440.0);

    SECTION("the same sound at another phase") {
        // Half a period at 440 Hz, which inverts the waveform and leaves the
        // spectrum alone.
        const auto shifted = delayFractionally(native, kSampleRate / 440.0 / 2.0);

        const auto waveform = compareAudio(native, shifted);
        CHECK_FALSE(waveform.withinFloor());

        const auto spectral = compareSpectra(native, shifted, 0);
        INFO("median " << spectral.medianDb << " p95 " << spectral.percentile95Db);
        REQUIRE(spectral.frames > 0);
        CHECK(spectral.percentile95Db < 1.0);
    }

    SECTION("a different pitch is a different sound") {
        const auto other = tone(2.0, 466.0);
        const auto spectral = compareSpectra(native, other, 0);

        REQUIRE(spectral.frames > 0);
        CHECK(spectral.percentile95Db > 3.0);
    }

    SECTION("material that stops early is a different sound") {
        auto truncated = tone(2.0, 440.0);
        truncated.clear(truncated.getNumSamples() / 2, truncated.getNumSamples() / 2);

        const auto spectral = compareSpectra(native, truncated, 0);

        REQUIRE(spectral.frames > 0);
        CHECK(spectral.percentile95Db > 3.0);
    }
}

TEST_CASE("A short render is not a null however well its prefix agrees", "[nulldiff][compare]") {
    // The residual is measured over what both renders cover, so a leg that came
    // back short with a bit-identical prefix would otherwise be certified as a
    // null while being exactly what this file calls a bug in a leg.
    const auto native = impulses(2.0);

    juce::AudioBuffer<float> truncated(native.getNumChannels(), native.getNumSamples() / 2);
    for (auto channel = 0; channel < truncated.getNumChannels(); ++channel)
        truncated.copyFrom(channel, 0, native, channel, 0, truncated.getNumSamples());

    const auto result = compareAudio(native, truncated);

    // The overlap really is identical, which is what makes this the trap it is.
    CHECK(result.withinFloor());
    CHECK(result.lengthDifference == native.getNumSamples() - truncated.getNumSamples());
    CHECK_FALSE(result.nulled());
}

TEST_CASE("Messages outside notes and controllers are compared, not counted",
          "[nulldiff][compare][midi]") {
    // An MPE note opens with a channel pressure, so this is a stream the corpus
    // asserts on. Equal counts used to be enough, which passed whatever channel,
    // value or instant they carried.
    const auto pressure = [](int channel, int value, double seconds) {
        return MidiEvent{samplesAt(seconds), static_cast<std::uint8_t>(0xD0 | (channel - 1)),
                         static_cast<std::uint8_t>(value), 0};
    };

    MidiStream native;
    native.push_back(pressure(2, 0, 0.0));
    native.push_back(noteOn(2, 60, 100, 0.0));
    native.push_back(noteOff(2, 60, 0.5));

    SECTION("the same message passes") {
        CHECK(compareMidi(native, native, midiOptions()).passed());
    }

    SECTION("a different value fails") {
        auto incumbent = native;
        incumbent[0] = pressure(2, 64, 0.0);
        const auto result = compareMidi(native, incumbent, midiOptions());
        CHECK_FALSE(result.otherMessagesMatch);
        CHECK_FALSE(result.passed());
    }

    SECTION("a different channel fails") {
        auto incumbent = native;
        incumbent[0] = pressure(3, 0, 0.0);
        CHECK_FALSE(compareMidi(native, incumbent, midiOptions()).passed());
    }

    SECTION("a different instant fails") {
        auto incumbent = native;
        incumbent[0] = pressure(2, 0, 0.25);
        CHECK_FALSE(compareMidi(native, incumbent, midiOptions()).passed());
    }

    SECTION("a missing message fails") {
        MidiStream incumbent{native[1], native[2]};
        CHECK_FALSE(compareMidi(native, incumbent, midiOptions()).passed());
    }
}

TEST_CASE("A short render is not a shift pass either", "[nulldiff][compare]") {
    // The stretched metrics both trim to the overlap, so the length has to be
    // checked before them rather than by them: aligned and short still agrees
    // everywhere it is looked at.
    const auto native = tone(2.0);

    juce::AudioBuffer<float> truncated(native.getNumChannels(), native.getNumSamples() / 2);
    for (auto channel = 0; channel < truncated.getNumChannels(); ++channel)
        truncated.copyFrom(channel, 0, native, channel, 0, truncated.getNumSamples());

    // Every metric a stretched case applies is happy with the pair.
    const auto envelope = compareEnvelopes(native, truncated, 0, kSampleRate);
    CHECK(std::abs(envelope.lagSamples) <= 1.0);

    const auto spectra = compareSpectra(native, truncated, 0);
    CHECK(spectra.frames > 0);
    CHECK(spectra.percentile95Db < 1.0);

    // The length is the only thing that says otherwise, which is why a
    // stretched verdict has to ask before it applies them.
    const auto result = compareAudio(native, truncated);
    CHECK(result.lengthDifference != 0);
    CHECK_FALSE(result.nulled());
}

TEST_CASE("Garbage is refused rather than certified", "[nulldiff][compare]") {
    // The adversarial case, and the one a comparator must never get wrong:
    // every comparison against a NaN is false, so a NaN residual never beats
    // the running peak. Left alone, the peak stays at zero, the level reads as
    // -inf and the pair is reported as a perfect null.
    const auto native = tone(0.5);

    const auto poisoned = [&native](float value, int at) {
        auto copy = native;
        copy.setSample(0, at, value);
        return copy;
    };

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();

    SECTION("a NaN in the incumbent") {
        const auto result = compareAudio(native, poisoned(nan, 100));
        CHECK_FALSE(result.refusal.empty());
        CHECK_FALSE(result.nulled());
        CHECK_FALSE(result.peakDb == -std::numeric_limits<double>::infinity());
    }

    SECTION("a NaN in the native render") {
        const auto result = compareAudio(poisoned(nan, 100), native);
        CHECK_FALSE(result.refusal.empty());
        CHECK_FALSE(result.nulled());
    }

    SECTION("an infinity in both, which would otherwise cancel") {
        const auto result = compareAudio(poisoned(infinity, 100), poisoned(infinity, 100));
        CHECK_FALSE(result.refusal.empty());
        CHECK_FALSE(result.nulled());
    }
}

TEST_CASE("Garbage outside the aligned overlap is refused too", "[nulldiff][compare][shift]") {
    // A shift excludes a margin at each end, and the envelope and spectral
    // comparisons trim the same margins. Garbage that lands in one of those is
    // read by nothing, so validating the overlap alone would let a stretched
    // case pass a render with a NaN in it.
    const auto native = tone(2.0);
    auto incumbent = delayed(native, 1024);

    // Inside the incumbent's leading margin: with the alignment applied, no
    // comparison ever reads this sample.
    incumbent.setSample(0, 10, std::numeric_limits<float>::quiet_NaN());

    AudioCompareOptions options;
    options.measureShift = true;
    const auto result = compareAudio(native, incumbent, options);

    INFO(result.refusal);
    CHECK_FALSE(result.refusal.empty());
    CHECK_FALSE(result.nulled());
}
