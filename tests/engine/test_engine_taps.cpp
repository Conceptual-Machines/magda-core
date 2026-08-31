#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

#include "tap/LevelTap.hpp"
#include "tap/SampleRing.hpp"

using magda::engine::LevelTap;
using magda::engine::SampleRing;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-6);
}

/// A stereo block whose channels differ, so a tap that folded them together
/// would be caught rather than merely suspected.
struct Block {
    Block(std::initializer_list<float> left, std::initializer_list<float> right)
        : samples(2, static_cast<int>(left.size())) {
        int index = 0;
        for (const auto value : left)
            samples.setSample(0, index++, value);
        index = 0;
        for (const auto value : right)
            samples.setSample(1, index++, value);
    }

    juce::dsp::AudioBlock<const float> block() const {
        return juce::dsp::AudioBlock<const float>(samples);
    }

    int numSamples() const {
        return samples.getNumSamples();
    }

    juce::AudioBuffer<float> samples;
};

}  // namespace

TEST_CASE("A level tap reports each channel's own peak", "[engine][tap]") {
    LevelTap tap;
    const Block block({0.25f, -0.5f}, {0.1f, 0.75f});

    tap.write(block.block(), block.numSamples());

    const auto levels = tap.read();
    CHECK(levels.peak[0] == approx(0.5f));
    CHECK(levels.peak[1] == approx(0.75f));
}

TEST_CASE("A level tap reports the loudest block since it was read, not the last one",
          "[engine][tap]") {
    LevelTap tap;

    // The transient is in the middle. A meter that reported the most recent
    // block would show the quiet one that followed it, which is how a drum hit
    // goes missing on a display that is not polling fast enough.
    tap.write(Block({0.2f}, {0.2f}).block(), 1);
    tap.write(Block({0.9f}, {0.9f}).block(), 1);
    tap.write(Block({0.1f}, {0.1f}).block(), 1);

    CHECK(tap.read().loudest() == approx(0.9f));
}

TEST_CASE("Reading a level tap starts it again", "[engine][tap]") {
    LevelTap tap;
    tap.write(Block({0.6f}, {0.6f}).block(), 1);

    REQUIRE(tap.read().loudest() == approx(0.6f));

    // Nothing has been written since, so the peak that was already reported is
    // not reported twice: a meter that held its value would never fall.
    CHECK(tap.read().loudest() == approx(0.0f));
}

TEST_CASE("Silence written to a level tap does not lower what has not been read", "[engine][tap]") {
    LevelTap tap;
    tap.write(Block({0.8f}, {0.8f}).block(), 1);
    tap.write(Block({0.0f}, {0.0f}).block(), 1);

    // How fast a meter falls is the reader's cadence. A silent block that took
    // the peak back down would make it the block size instead, so a hit landing
    // just before a poll would be missed at one buffer size and caught at
    // another.
    CHECK(tap.read().loudest() == approx(0.8f));
}

TEST_CASE("A level tap reports a mono block on both channels", "[engine][tap]") {
    LevelTap tap;
    juce::AudioBuffer<float> mono(1, 1);
    mono.setSample(0, 0, 0.4f);

    tap.write(juce::dsp::AudioBlock<const float>(mono), 1);

    const auto levels = tap.read();
    CHECK(levels.peak[0] == approx(0.4f));
    CHECK(levels.peak[1] == approx(0.4f));
}

TEST_CASE("A cleared level tap has nothing to report", "[engine][tap]") {
    LevelTap tap;
    tap.write(Block({0.9f}, {0.9f}).block(), 1);

    tap.clear();

    CHECK(tap.read().loudest() == approx(0.0f));
}

TEST_CASE("A sample ring hands back the samples most recently written", "[engine][tap]") {
    SampleRing ring(1024);
    std::vector<float> written(300);
    for (std::size_t i = 0; i < written.size(); ++i)
        written[i] = static_cast<float>(i);

    ring.write(written.data(), static_cast<int>(written.size()));

    std::vector<float> read(4);
    CHECK(ring.readLatest(read.data(), 4) == written.size());
    CHECK(read[0] == approx(296.0f));
    CHECK(read[3] == approx(299.0f));
}

TEST_CASE("A sample ring that has not filled once pads with silence", "[engine][tap]") {
    SampleRing ring(1024);
    const float sample = 1.0f;
    ring.write(&sample, 1);

    std::vector<float> read(4);
    ring.readLatest(read.data(), 4);

    // Silence in front rather than whatever the allocation held: a scope opened
    // a moment ago draws a flat line, not noise.
    CHECK(read[0] == approx(0.0f));
    CHECK(read[2] == approx(0.0f));
    CHECK(read[3] == approx(1.0f));
}

TEST_CASE("A sample ring keeps the newest samples once it has wrapped", "[engine][tap]") {
    SampleRing ring(1024);
    const auto capacity = ring.capacity();

    std::vector<float> written(static_cast<std::size_t>(capacity) + 100);
    for (std::size_t i = 0; i < written.size(); ++i)
        written[i] = static_cast<float>(i);
    ring.write(written.data(), static_cast<int>(written.size()));

    std::vector<float> read(2);
    ring.readLatest(read.data(), 2);
    CHECK(read[1] == approx(static_cast<float>(written.size() - 1)));
}

TEST_CASE("A sample ring pads a window longer than it is deep", "[engine][tap]") {
    // A scope on a long timebase asks for more history than the ring holds.
    // What it must not get is the ring twice: the masked index folds two laps
    // onto the same slots, and the second copy would draw as signal that was
    // never played.
    SampleRing ring(1024);
    const auto capacity = ring.capacity();

    std::vector<float> written(static_cast<std::size_t>(capacity) * 2);
    for (std::size_t i = 0; i < written.size(); ++i)
        written[i] = 1.0f + static_cast<float>(i);
    ring.write(written.data(), static_cast<int>(written.size()));

    std::vector<float> read(static_cast<std::size_t>(capacity) * 2);
    ring.readLatest(read.data(), static_cast<int>(read.size()));

    // The half the ring cannot answer for is silence, not a repeat.
    for (auto sample = 0; sample < capacity; ++sample) {
        INFO("sample " << sample);
        REQUIRE(read[static_cast<std::size_t>(sample)] == approx(0.0f));
    }

    // The half it can is the newest audio, in order.
    CHECK(read[static_cast<std::size_t>(capacity)] == approx(written[written.size() - capacity]));
    CHECK(read.back() == approx(written.back()));
}

TEST_CASE("A sample ring downmixes without a maximum block size", "[engine][tap]") {
    SampleRing ring(1024);

    // Longer than anything a prepare could have been told about. Writing
    // straight into the ring is what makes that a non-question; a tap with
    // scratch would have to drop the block or allocate.
    const auto numSamples = ring.capacity() / 2;
    juce::AudioBuffer<float> stereo(2, numSamples);
    for (int sample = 0; sample < numSamples; ++sample) {
        stereo.setSample(0, sample, 1.0f);
        stereo.setSample(1, sample, 0.0f);
    }

    ring.writeDownmix(juce::dsp::AudioBlock<const float>(stereo), numSamples);

    std::vector<float> read(1);
    ring.readLatest(read.data(), 1);
    CHECK(read[0] == approx(0.5f));
    CHECK(ring.writePosition() == static_cast<std::size_t>(numSamples));
}

TEST_CASE("A sample ring reads while it is being written", "[engine][tap]") {
    // The SPSC claim, across two threads, which is where the atomic slots earn
    // themselves: under `make tsan` a plain float ring laps the reader and
    // reports a race. What is asserted is only what the class promises, that a
    // read returns samples the writer has written and the position never goes
    // backwards. Which samples is deliberately not asserted: the reader is
    // allowed to be lapped, and that is the documented bound rather than a
    // failure.
    SampleRing ring(1024);
    constexpr int kWrites = 20000;
    std::atomic<bool> writing{true};
    std::atomic<bool> readerReady{false};

    // The writer waits to be joined before it starts. Without the handshake the
    // whole run can finish before this thread first looks at `writing`, which
    // is a legal schedule on a loaded CI box and would leave the test asserting
    // about an overlap that never happened.
    std::thread writer([&] {
        while (!readerReady) {
        }

        for (auto i = 0; i < kWrites; ++i) {
            const auto sample = static_cast<float>(i % 100) * 0.01f;
            ring.write(&sample, 1);
        }
        writing = false;
    });

    std::vector<float> read(64);
    std::size_t lastPosition = 0;
    auto reads = 0;

    readerReady = true;
    while (writing) {
        const auto position = ring.readLatest(read.data(), static_cast<int>(read.size()));
        REQUIRE(position >= lastPosition);
        lastPosition = position;
        ++reads;
    }

    writer.join();

    CHECK(reads > 0);
    CHECK(ring.writePosition() == static_cast<std::size_t>(kWrites));

    // Whatever it was lapped by along the way, the ring settles on the last
    // samples written once the writer stops.
    ring.readLatest(read.data(), static_cast<int>(read.size()));
    CHECK(read.back() == approx(static_cast<float>((kWrites - 1) % 100) * 0.01f));
}

TEST_CASE("A level tap loses nothing to a reader running beside the writer", "[engine][tap]") {
    LevelTap tap;

    // The one thing the compare-and-swap in accumulate() is there for: a read
    // landing between a write's load and its store must not put back a peak
    // that has already been reported, and must not swallow one that has not.
    // The loudest sample is written once, so exactly one read has to see it.
    constexpr int kWrites = 20000;
    std::atomic<bool> writing{true};

    std::thread writer([&] {
        for (auto i = 0; i < kWrites; ++i) {
            const auto level = i == kWrites / 2 ? 1.0f : 0.25f;
            juce::AudioBuffer<float> buffer(2, 1);
            buffer.setSample(0, 0, level);
            buffer.setSample(1, 0, level);
            tap.write(juce::dsp::AudioBlock<const float>(buffer), 1);
        }
        writing = false;
    });

    auto sawTheTransient = false;
    while (writing)
        sawTheTransient |= tap.read().loudest() > 0.9f;
    sawTheTransient |= tap.read().loudest() > 0.9f;

    writer.join();
    CHECK(sawTheTransient);
}
