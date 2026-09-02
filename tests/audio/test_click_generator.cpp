#include <catch2/catch_test_macros.hpp>

#include "transport/ClickGenerator.hpp"

using magda::engine::BlockInfo;
using magda::engine::ClickGenerator;
using magda::engine::ClickSettings;
using magda::engine::RenderContext;
using magda::engine::TempoMap;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

/// 120 bpm: 22050 samples to the beat.
constexpr double kSamplesPerBeat = kSampleRate / 2.0;

BlockInfo blockFrom(double startBeat, int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.beats.start = startBeat;
    block.beats.end = startBeat + numSamples / kSamplesPerBeat;
    block.continuous = true;
    return block;
}

/// The first sample past @p from with anything in it, or -1.
int firstSounding(const juce::AudioBuffer<float>& output, int from = 0) {
    for (auto sample = from; sample < output.getNumSamples(); ++sample)
        if (output.getSample(0, sample) != 0.0f)
            return sample;
    return -1;
}

/// Where a click begins, or -1. Its very first sample is silent wherever it
/// lands, because a sine starts at zero, so the burst begins one before the
/// first thing audible.
int clickStart(const juce::AudioBuffer<float>& output) {
    const auto sounding = firstSounding(output);
    return sounding < 0 ? sounding : sounding - 1;
}

struct Fixture {
    Fixture() {
        generator.prepare(RenderContext{kSampleRate, kBlockSize, 2});
        output.setSize(2, kBlockSize);
        output.clear();
    }

    void render(const BlockInfo& block, bool countingIn = false) {
        generator.render(tempo, click, block, countingIn, output, 0);
    }

    ClickGenerator generator;
    TempoMap tempo;
    ClickSettings click{true, true, 1.0f};
    juce::AudioBuffer<float> output;
};

}  // namespace

TEST_CASE("The metronome sounds where the beat is", "[engine][transport][click]") {
    Fixture fixture;

    SECTION("on the first sample of a block that starts on a beat") {
        fixture.render(blockFrom(0.0, kBlockSize));
        CHECK(clickStart(fixture.output) == 0);
    }

    SECTION("part way into a block that does not") {
        // The beat falls a hundred samples in, which is where the click starts
        // and not a block early or a block late.
        fixture.render(blockFrom(1.0 - 100.0 / kSamplesPerBeat, kBlockSize));
        CHECK(clickStart(fixture.output) == 100);
    }

    SECTION("not at all between beats") {
        fixture.render(blockFrom(0.25, kBlockSize));
        CHECK(firstSounding(fixture.output) == -1);
    }

    SECTION("not at all while stopped") {
        auto block = blockFrom(0.0, kBlockSize);
        block.playing = false;
        block.beats.end = block.beats.start;

        fixture.render(block);
        CHECK(firstSounding(fixture.output) == -1);
    }
}

TEST_CASE("A bar is accented", "[engine][transport][click]") {
    // A generator apiece, because a click carries into the blocks after it and
    // one rendered on top of another's tail is not the sound being compared.
    Fixture onTheBar, offTheBar;

    onTheBar.render(blockFrom(0.0, kBlockSize));
    offTheBar.render(blockFrom(1.0, kBlockSize));

    REQUIRE(clickStart(onTheBar.output) == 0);
    REQUIRE(clickStart(offTheBar.output) == 0);

    auto different = false;
    for (auto sample = 0; sample < kBlockSize; ++sample)
        different = different ||
                    onTheBar.output.getSample(0, sample) != offTheBar.output.getSample(0, sample);
    CHECK(different);

    SECTION("unless the emphasis is off, and then every beat is a beat") {
        Fixture plain;
        plain.click.emphasiseBars = false;
        plain.render(blockFrom(0.0, kBlockSize));

        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(plain.output.getSample(0, sample) == offTheBar.output.getSample(0, sample));
    }
}

TEST_CASE("A click outlives the block it starts in", "[engine][transport][click]") {
    Fixture fixture;

    // A click is forty milliseconds, which is several blocks of five hundred
    // and twelve samples: what carries is the only state the metronome has.
    fixture.render(blockFrom(0.0, kBlockSize));
    REQUIRE(fixture.output.getSample(0, kBlockSize - 1) != 0.0f);

    fixture.output.clear();
    fixture.render(blockFrom(kBlockSize / kSamplesPerBeat, kBlockSize));
    CHECK(fixture.output.getSample(0, 0) != 0.0f);

    SECTION("including across a jump, which is not the metronome's business") {
        fixture.output.clear();

        auto jumped = blockFrom(64.0, kBlockSize);
        jumped.continuous = false;

        fixture.render(jumped);
        CHECK(fixture.output.getSample(0, 0) != 0.0f);
    }
}

TEST_CASE("The metronome is off when it is off, and on for a count-in",
          "[engine][transport][click][countin]") {
    Fixture fixture;
    fixture.click.enabled = false;

    SECTION("switched off") {
        fixture.render(blockFrom(0.0, kBlockSize));
        CHECK(firstSounding(fixture.output) == -1);
    }

    SECTION("switched off, but counting in") {
        // A count-in that did not count would just be a late start.
        fixture.render(blockFrom(0.0, kBlockSize), true);
        CHECK(clickStart(fixture.output) == 0);
    }

    SECTION("switched off during a click, which finishes rather than snapping") {
        fixture.click.enabled = true;
        fixture.render(blockFrom(0.0, kBlockSize));
        REQUIRE(fixture.output.getSample(0, kBlockSize - 1) != 0.0f);

        fixture.click.enabled = false;
        fixture.output.clear();
        fixture.render(blockFrom(kBlockSize / kSamplesPerBeat, kBlockSize));
        CHECK(fixture.output.getSample(0, 0) != 0.0f);
    }
}

TEST_CASE("The metronome follows the time signature", "[engine][transport][click]") {
    Fixture fixture;
    fixture.tempo = TempoMap({}, {{0.0, 6, 8}});

    // Six eighths to the bar: a click every half beat rather than every beat.
    fixture.render(blockFrom(0.5 - 100.0 / kSamplesPerBeat, kBlockSize));
    CHECK(clickStart(fixture.output) == 100);
}

TEST_CASE("Two beats inside one block both sound", "[engine][transport][click]") {
    Fixture fixture;

    // A block long enough to hold a whole beat, at a tempo where one fits:
    // the metronome is not once per callback.
    juce::AudioBuffer<float> output(2, static_cast<int>(kSamplesPerBeat) + 200);
    output.clear();

    auto block = blockFrom(0.0, output.getNumSamples());
    fixture.generator.render(fixture.tempo, fixture.click, block, false, output, 0);

    CHECK(clickStart(output) == 0);
    CHECK(output.getSample(0, static_cast<int>(kSamplesPerBeat) + 1) != 0.0f);
}
