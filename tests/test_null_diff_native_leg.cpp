#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <string>

#include "NullDiffNativeLeg.hpp"

/**
 * The native leg of the null-diff corpus, on its own (#2040).
 *
 * A leg that returns silence makes every case it touches compare two silences,
 * and two silences null perfectly. So before either leg is compared against the
 * other, each has to be caught lying about itself. This is that check for the
 * native one: every case renders, renders something, and renders it without the
 * engine reporting that it dropped anything on the way.
 *
 * It runs in the model-only target because the native engine needs no Edit. The
 * incumbent leg gets the same treatment in the JUCE target, where it can be
 * caught rendering before its proxies arrived.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_null_diff_native_leg");
    root.createDirectory();
    return root;
}

double peakOf(const juce::AudioBuffer<float>& buffer) {
    return buffer.getNumSamples() > 0
               ? static_cast<double>(buffer.getMagnitude(0, buffer.getNumSamples()))
               : 0.0;
}

}  // namespace

TEST_CASE("Every case renders through the native engine", "[nulldiff][native]") {
    for (const auto& value : buildCorpus(scratch())) {
        INFO(value.name);

        const auto rendered = renderNative(value);

        CHECK(rendered.failure.empty());

        // Nothing the engine could not do. A snapshot that dropped a clip and a
        // render that matched it are two bugs rather than none, so a diagnostic
        // fails here whatever the audio did.
        for (const auto& diagnostic : rendered.diagnostics)
            INFO(diagnostic);
        CHECK(rendered.diagnostics.empty());

        CHECK(rendered.starvedVoices == 0);
        CHECK(rendered.droppedMidiEvents == 0);

        const auto expectedSamples = static_cast<std::int64_t>(std::llround(
            (value.endBeat - value.startBeat) * 60.0 / value.startBpm() * value.sampleRate));

        if (value.capturesMidi()) {
            // A MIDI case renders no audio by construction: the device standing
            // in for a synth records rather than sounds. What it must not do is
            // record nothing.
            CHECK_FALSE(rendered.midi.empty());
        } else {
            CHECK(rendered.audio.getNumSamples() > 0);
            CHECK(peakOf(rendered.audio) > 0.01);

            // Only for a case at one tempo: with a change in it, the range is
            // shorter than the arithmetic above says and the map is the one
            // that knows by how much.
            if (value.tempo.size() == 1)
                CHECK(std::abs(rendered.audio.getNumSamples() - expectedSamples) <= 1);
        }
    }
}

TEST_CASE("The native leg renders the same at any block size", "[nulldiff][native]") {
    // What RenderContext requires of every op, now over real material rather
    // than over a test device: block size is an I/O batching concept and never
    // a precision one.
    //
    // Two kinds of clip do not hold to it today, and both were found by running
    // this over the corpus. They are named rather than skipped by a predicate,
    // so that the list cannot quietly grow: a case that starts failing this has
    // to be added here by somebody, with a reason.
    //
    // This used to hold five cases and now holds one, which is the corpus
    // paying for itself. A rate that varies within a block was resolved from
    // that block's own two ends, so a speed ramp, auto tempo across a tempo
    // change and a warped clip approximated their curve differently at every
    // block size; and a stretcher framed whatever sizes it was handed. Both are
    // gone: a clip that consumes its reading at a rate is now fed on a grid
    // anchored to where its event begins (ClipVoice::renderThroughCells), so
    // what reaches the stretcher is a function of position on the timeline.
    //
    // What is left is one SoundTouch mode. Its sibling, the same material and
    // the same feed, holds; so does Signalsmith. Whatever is left in there is
    // internal to that mode rather than to how it is fed, and it is worth its
    // own issue rather than a guess here.
    const std::set<std::string> knownDependent{
        "stretch.soundtouch.normal",
    };

    std::set<std::string> failed;

    for (const auto& value : buildCorpus(scratch())) {
        if (value.capturesMidi())
            continue;

        INFO(value.name);

        auto small = value;
        small.blockSize = 128;
        auto large = value;
        large.blockSize = 1024;

        const auto first = renderNative(small);
        const auto second = renderNative(large);

        REQUIRE(first.failure.empty());
        REQUIRE(second.failure.empty());
        REQUIRE(first.audio.getNumSamples() == second.audio.getNumSamples());

        AudioCompareOptions options;
        options.sampleRate = value.sampleRate;
        const auto residual = compareAudio(first.audio, second.audio, options);

        INFO("peak " << formatDb(residual.peakDb) << " at sample " << residual.firstDivergence);

        if (!residual.withinFloor())
            failed.insert(value.name);

        if (knownDependent.count(value.name) == 0)
            CHECK(residual.withinFloor());
    }

    // The list is exactly the cases that fail, in both directions. One that
    // stops failing has to come off the list, so that a fix is recorded rather
    // than absorbed.
    CHECK(failed == knownDependent);
}
