#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "clip/FadeCurves.hpp"

using namespace magda::engine;
using Catch::Approx;

namespace {

constexpr int kChannels = 2;

/// A buffer and a block over it, so a test writes samples rather than plumbing.
struct Signal {
    explicit Signal(int numSamples, float value = 0.0f)
        : buffer(kChannels, numSamples), block(buffer) {
        buffer.clear();

        if (value != 0.0f)
            for (auto channel = 0; channel < kChannels; ++channel)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), value,
                                                  numSamples);
    }

    float at(int sample, int channel = 0) const {
        return buffer.getSample(channel, sample);
    }

    /// Silence from @p from onwards, which is what a source leaves behind when
    /// it stops partway through a block.
    void clearFrom(int from) {
        for (auto channel = 0; channel < kChannels; ++channel)
            buffer.clear(channel, from, buffer.getNumSamples() - from);
    }

    juce::AudioBuffer<float> buffer;
    juce::dsp::AudioBlock<float> block;
};

}  // namespace

TEST_CASE("A stop carries the last sample down to zero", "[engine][clip][declick]") {
    // A source at full swing that stops a third of the way into the block. What
    // it leaves behind is a step from 1 to 0, which is the click.
    Signal signal(64, 1.0f);
    signal.clearFrom(20);

    StopDeClick deClick;
    deClick.begin(signal.block, 20, 16);

    SECTION("the tail begins where the signal was") {
        REQUIRE(signal.at(20) == Approx(1.0f));
    }

    SECTION("and reaches zero by the end of the ramp") {
        REQUIRE(signal.at(35) == Approx(0.0f).margin(1e-6));
        REQUIRE(signal.at(36) == Approx(0.0f));
        REQUIRE(signal.at(63) == Approx(0.0f));
    }

    SECTION("monotonically, because a decay that overshoots is its own click") {
        for (auto sample = 21; sample <= 35; ++sample) {
            INFO("sample " << sample);
            REQUIRE(signal.at(sample) <= signal.at(sample - 1));
        }
    }

    SECTION("and it does not touch what sounded before it") {
        for (auto sample = 0; sample < 20; ++sample) {
            INFO("sample " << sample);
            REQUIRE(signal.at(sample) == Approx(1.0f));
        }
    }
}

TEST_CASE("A stop on a block boundary steps down from the block before it",
          "[engine][clip][declick]") {
    // The same stop, one sample later, so it lands on the first sample of the
    // next callback. Which side of a boundary it fell on is not something a
    // stop should be able to hear.
    StopDeClick deClick;

    Signal sounded(64, 1.0f);
    deClick.push(sounded.block);

    Signal after(64);
    deClick.begin(after.block, 0, 16);

    REQUIRE(after.at(0) == Approx(1.0f));
    REQUIRE(after.at(15) == Approx(0.0f).margin(1e-6));
    REQUIRE(after.at(16) == Approx(0.0f));
}

TEST_CASE("A stop ramp longer than the block carries on into the next", "[engine][clip][declick]") {
    // Block size is an I/O batching concept and never a precision one: the same
    // stop has to come out the same however the render was cut up.
    StopDeClick deClick;

    Signal first(32, 1.0f);
    first.clearFrom(24);
    deClick.begin(first.block, 24, 64);

    REQUIRE(deClick.active());
    REQUIRE(first.at(24) == Approx(1.0f));
    REQUIRE(first.at(31) > 0.0f);

    Signal second(64);
    deClick.advance(second.block);

    SECTION("picking up where the first block left off") {
        // Continuous across the seam: the 8th sample of the ramp ended the
        // first block, the 9th begins this one.
        REQUIRE(second.at(0) < first.at(31));
        REQUIRE(second.at(0) > 0.0f);
    }

    SECTION("and ending where a ramp of that length should") {
        // Eight of the ramp's 64 samples went into the first block, so the
        // remaining 56 end this one at index 55.
        REQUIRE(second.at(55) == Approx(0.0f).margin(1e-6));
        REQUIRE(second.at(56) == Approx(0.0f));
        REQUIRE(!deClick.active());
    }

    SECTION("after which asking again costs nothing") {
        Signal third(64, 0.5f);
        deClick.advance(third.block);
        REQUIRE(third.at(0) == Approx(0.5f));
    }
}

TEST_CASE("A stop on a zero crossing adds nothing at all", "[engine][clip][declick]") {
    Signal signal(64);

    StopDeClick deClick;
    deClick.begin(signal.block, 20, 16);

    for (auto sample = 0; sample < 64; ++sample) {
        INFO("sample " << sample);
        REQUIRE(signal.at(sample) == Approx(0.0f));
    }
}

TEST_CASE("A stop ramp of zero leaves the step exactly as it was", "[engine][clip][declick]") {
    // The dual of a launch ramp of zero preserving the leading transient: a
    // caller that asked for no de-click gets the material and nothing else.
    Signal signal(64, 1.0f);
    signal.clearFrom(20);

    StopDeClick deClick;
    deClick.begin(signal.block, 20, 0);

    REQUIRE(signal.at(19) == Approx(1.0f));
    REQUIRE(signal.at(20) == Approx(0.0f));
    REQUIRE(!deClick.active());
}

TEST_CASE("A second stop steps down from where the signal is now", "[engine][clip][declick]") {
    // Two switches inside one block, which one ramp cannot both complete. The
    // second does not resume the first: it decays what is actually there, which
    // is the only value continuous with what the listener just heard.
    StopDeClick deClick;

    Signal signal(64, 1.0f);
    signal.clearFrom(8);
    deClick.begin(signal.block, 8, 32);

    // The other side comes back at half the level, and goes again.
    for (auto channel = 0; channel < kChannels; ++channel)
        juce::FloatVectorOperations::fill(signal.buffer.getWritePointer(channel) + 16, 0.5f, 8);
    signal.clearFrom(24);

    deClick.begin(signal.block, 24, 32);

    REQUIRE(signal.at(24) == Approx(0.5f));
    REQUIRE(signal.at(55) == Approx(0.0f).margin(1e-6));
}

TEST_CASE("The two de-clicks are mirrors of one another", "[engine][clip][declick]") {
    // A start subtracts the step the material begins on; a stop carries the one
    // it ends on. Run over the same step in opposite directions they describe
    // the same curve, which is the property that says the pair is one idea and
    // not two.
    constexpr int kLength = 32;

    Signal starting(kLength, 1.0f);
    StartDeClick start;
    start.begin(starting.block, kLength);

    Signal stopping(kLength);
    StopDeClick stop;
    Signal sounded(4, 1.0f);
    stop.push(sounded.block);
    stop.begin(stopping.block, 0, kLength);

    for (auto sample = 0; sample < kLength; ++sample) {
        INFO("sample " << sample);
        REQUIRE(starting.at(sample) + stopping.at(sample) == Approx(1.0f));
    }
}
