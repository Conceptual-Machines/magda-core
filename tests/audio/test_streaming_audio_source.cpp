#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

#include "io/PrefetchThread.hpp"
#include "io/StreamingAudioSource.hpp"

using magda::engine::AudioFileReader;
using magda::engine::BlockInfo;
using magda::engine::ClipPlacement;
using magda::engine::PrefetchStream;
using magda::engine::PrefetchThread;
using magda::engine::RenderContext;
using magda::engine::StreamingAudioSource;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;

/// As in the prefetch tests: sample n reads back as n, so where a sample came
/// from is readable off the value.
class CountingReader final : public AudioFileReader {
  public:
    explicit CountingReader(std::int64_t length) : length_(length) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }

    double sampleRate() const override {
        return kSampleRate;
    }

    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        const auto available =
            static_cast<int>(std::clamp<std::int64_t>(length_ - startSample, 0, numSamples));

        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < available; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample));

        if (available < numSamples)
            destination.clear(destinationOffset + available, numSamples - available);

        return available;
    }

  private:
    std::int64_t length_;
};

RenderContext context() {
    return RenderContext{kSampleRate, kBlockSize, 2};
}

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

/// A block covering a stretch of the timeline. Seconds are what a clip is
/// placed in; the beats are along for the ride and nothing here reads them.
BlockInfo blockFrom(double startSeconds, int numSamples = kBlockSize, bool continuous = true) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.sampleRate = kSampleRate;
    block.monotonicSamples = {
        magda::engine::SamplePosition{std::llround(startSeconds * kSampleRate)},
        magda::engine::SamplePosition{std::llround(startSeconds * kSampleRate) + numSamples}};
    block.playing = true;
    block.seconds.start = startSeconds;
    block.seconds.end = startSeconds + numSamples / kSampleRate;
    block.beats.start = startSeconds * 2.0;  // 120 bpm, which nothing here depends on
    block.beats.end = block.seconds.end * 2.0;
    block.continuous = continuous;
    return block;
}

struct Fixture {
    explicit Fixture(const ClipPlacement& placementIn, std::int64_t length = 1000000)
        : placement(placementIn),
          stream(std::make_unique<CountingReader>(length), context(), {256, 8}),
          source(stream, placementIn) {
        source.prepare(context());
        output.setSize(2, kBlockSize);
        output.clear();
    }

    /// Point the stream at where this block will want the file, which is what
    /// whoever schedules a clip does: the first block at a position nobody
    /// mentioned has to seek, and a seek costs a block of silence.
    void cue(const BlockInfo& block) {
        stream.seek(source.sourceSampleAt(std::max(block.seconds.start, placement.seconds.start)));
    }

    /// Cue, let a callback take the cue up, read ahead, and render: what a
    /// clip that was scheduled properly sounds like. The fold-in stands in for
    /// the block before this one, which is where a live engine would do it.
    void renderCued(const BlockInfo& block) {
        cue(block);
        stream.applyPendingCue();
        render(block);
    }

    /// Read ahead as far as the pool allows, then render.
    void render(const BlockInfo& block) {
        while (stream.fill()) {
        }

        output.clear();
        source.render(block, juce::dsp::AudioBlock<float>(output));
    }

    float at(int sample) const {
        return output.getSample(0, sample);
    }

    ClipPlacement placement;
    PrefetchStream stream;
    StreamingAudioSource source;
    juce::AudioBuffer<float> output;
};

}  // namespace

TEST_CASE("A clip plays the part of the block it covers", "[engine][io][clip]") {
    // Two seconds of timeline, playing the file from its tenth sample.
    const ClipPlacement placement{{1.0, 3.0}, 10};

    SECTION("nothing before it starts") {
        Fixture fixture(placement);
        fixture.render(blockFrom(0.5));

        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(fixture.at(sample) == approx(0.0f));
    }

    SECTION("silence up to its first sample, then the file") {
        Fixture fixture(placement);

        // The clip starts a third of the way into this block.
        const auto blockStart = 1.0 - 20.0 / kSampleRate;
        fixture.renderCued(blockFrom(blockStart));

        for (auto sample = 0; sample < 20; ++sample) {
            INFO("sample " << sample);
            REQUIRE(fixture.at(sample) == approx(0.0f));
        }

        // And from there it is the file, from where the clip was trimmed to.
        for (auto sample = 20; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(fixture.at(sample) == approx(static_cast<float>(10 + sample - 20)));
        }
    }

    SECTION("and it stops where the clip ends") {
        Fixture fixture(placement);

        const auto blockStart = 3.0 - 20.0 / kSampleRate;
        fixture.renderCued(blockFrom(blockStart));

        REQUIRE(fixture.at(0) != approx(0.0f));
        for (auto sample = 20; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(fixture.at(sample) == approx(0.0f));
        }
    }

    SECTION("nothing at all once it is over") {
        Fixture fixture(placement);
        fixture.render(blockFrom(3.5));

        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(fixture.at(sample) == approx(0.0f));
    }
}

TEST_CASE("A clip plays in step with the timeline it is placed on", "[engine][io][clip]") {
    const ClipPlacement placement{{0.0, 10.0}, 0};
    Fixture fixture(placement);

    // Block after block, the file advances one sample per sample and never
    // slips, which is what would happen if the source tracked its own position
    // instead of deriving it from where the block says it is.
    for (auto block = 0; block < 40; ++block) {
        const auto start = static_cast<double>(block * kBlockSize) / kSampleRate;
        fixture.render(blockFrom(start));

        INFO("block " << block);
        REQUIRE(fixture.at(0) == approx(static_cast<float>(block * kBlockSize)));
        REQUIRE(fixture.at(kBlockSize - 1) == approx(static_cast<float>(block * kBlockSize + 63)));
    }

    // One position, followed forwards: nothing here is a seek.
    CHECK(fixture.stream.underruns() == 0);
}

TEST_CASE("A jump is a seek, and needs nothing said about it", "[engine][io][clip]") {
    const ClipPlacement placement{{0.0, 10.0}, 0};
    Fixture fixture(placement);

    for (auto block = 0; block < 4; ++block)
        fixture.render(blockFrom(static_cast<double>(block * kBlockSize) / kSampleRate));

    SECTION("a loop wrap plays the file from where the timeline went") {
        // What a wrap looks like from down here: a block that does not
        // continue the last one, covering a stretch of timeline somewhere
        // else. The source derives its position from the block rather than
        // from what it played last, so it needs no telling that one happened.
        //
        // Nobody pointed the reader at the top of the loop, so the block that
        // wraps is silent and the one after it plays. That is the cost of a
        // jump nobody saw coming, and it is why a loop worth reading ahead for
        // is one whose return the clip layer cues.
        fixture.render(blockFrom(0.0, kBlockSize, false));
        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(fixture.at(sample) == approx(0.0f));
        REQUIRE(fixture.stream.underruns() == 1);

        fixture.render(blockFrom(static_cast<double>(kBlockSize) / kSampleRate));
        REQUIRE(fixture.at(0) == approx(static_cast<float>(kBlockSize)));
        REQUIRE(fixture.at(63) == approx(static_cast<float>(kBlockSize + 63)));
    }

    SECTION("a locate forwards is the same shape") {
        // Silence in the block that jumps, material in the one after it, and
        // one underrun to say so. Cueing does not shorten this: the stream is
        // sounding, and a cue given to a stream that is sounding waits.
        fixture.render(blockFrom(5.0));
        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(fixture.at(sample) == approx(0.0f));
        REQUIRE(fixture.stream.underruns() == 1);

        fixture.render(blockFrom(5.0 + static_cast<double>(kBlockSize) / kSampleRate));

        const auto expected = static_cast<float>(std::llround(5.0 * kSampleRate) + kBlockSize);
        REQUIRE(fixture.at(0) == approx(expected));
    }
}

TEST_CASE("A clip renders nothing while the transport is stopped", "[engine][io][clip]") {
    const ClipPlacement placement{{0.0, 10.0}, 0};
    Fixture fixture(placement);

    auto stopped = blockFrom(1.0);
    stopped.playing = false;
    stopped.seconds.end = stopped.seconds.start;
    stopped.beats.end = stopped.beats.start;

    fixture.render(stopped);

    // A stopped timeline is not a held sample. It is no material at all.
    for (auto sample = 0; sample < kBlockSize; ++sample)
        REQUIRE(fixture.at(sample) == approx(0.0f));
}

TEST_CASE("The prefetch thread reads for the streams registered with it",
          "[engine][io][prefetch]") {
    PrefetchStream stream(std::make_unique<CountingReader>(100000), context(), {256, 4});
    PrefetchThread thread;

    CHECK(thread.streamCount() == 0);
    thread.add(stream);
    CHECK(thread.streamCount() == 1);

    // The thread polls, so this is a wait rather than a check: what is being
    // tested is that it reads at all without anything asking it to, not how
    // quickly.
    juce::AudioBuffer<float> output(2, kBlockSize);
    auto delivered = 0;
    std::int64_t position = 0;

    // A callback that came up short moves on to the next block rather than
    // asking again: asking again would be asking for a position the stream has
    // already gone past, which is a seek, and a test that seeks every attempt
    // would never let the reader get ahead.
    for (auto attempt = 0; attempt < 2000 && delivered == 0; ++attempt) {
        delivered = stream.read(position, juce::dsp::AudioBlock<float>(output), kBlockSize);
        if (delivered == 0) {
            position += kBlockSize;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    CHECK(delivered == kBlockSize);
    CHECK(output.getSample(0, 0) == approx(static_cast<float>(position)));
    CHECK(output.getSample(0, 63) == approx(static_cast<float>(position + 63)));

    // Removal waits for the thread to be out of the stream, which is what
    // makes destroying it afterwards safe.
    thread.remove(stream);
    CHECK(thread.streamCount() == 0);
}

TEST_CASE("A file at another rate plays wrong, not broken", "[engine][io][clip]") {
    // 48 kHz material in a 44.1 kHz session. Resampling belongs to the clip
    // layer (#1890); until it exists, what this layer owes is playback that is
    // continuous and pitched wrong, rather than a position derived one way and
    // consumed another, which would land somewhere unexpected every block and
    // spend every callback seeking.
    class MismatchedReader final : public AudioFileReader {
      public:
        std::int64_t lengthInSamples() const override {
            return 1000000;
        }
        double sampleRate() const override {
            return 48000.0;
        }
        int numChannels() const override {
            return 2;
        }
        int read(juce::AudioBuffer<float>& destination, int destinationOffset,
                 std::int64_t startSample, int numSamples) override {
            for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
                for (auto sample = 0; sample < numSamples; ++sample)
                    destination.setSample(channel, destinationOffset + sample,
                                          static_cast<float>(startSample + sample));
            return numSamples;
        }
    };

    PrefetchStream stream(std::make_unique<MismatchedReader>(), context(), {256, 8});
    StreamingAudioSource source(stream, ClipPlacement{{0.0, 10.0}, 0});
    source.prepare(context());

    juce::AudioBuffer<float> output(2, kBlockSize);

    for (auto block = 0; block < 20; ++block) {
        while (stream.fill()) {
        }

        output.clear();
        source.render(blockFrom(static_cast<double>(block * kBlockSize) / kSampleRate),
                      juce::dsp::AudioBlock<float>(output));

        // Every sample of the file, in order, with none skipped between blocks.
        INFO("block " << block);
        REQUIRE(output.getSample(0, 0) == approx(static_cast<float>(block * kBlockSize)));
        REQUIRE(output.getSample(0, kBlockSize - 1) ==
                approx(static_cast<float>(block * kBlockSize + kBlockSize - 1)));
    }

    // Which is to say: not one seek in twenty blocks.
    CHECK(stream.underruns() == 0);
}

TEST_CASE("A clip cued before it starts plays without a gap", "[engine][io][prefetch]") {
    // The case cueing exists for: a clip that has not started, on a transport
    // that is already rolling. Nothing reads this stream yet, so nothing would
    // notice a cue at all if the source did not offer one every block.
    const ClipPlacement placement{{5.0, 10.0}, 0};
    Fixture fixture(placement);

    const auto target = fixture.source.sourceSampleAt(5.0);

    // Published from a thread that is not the audio one, touching nothing the
    // callback owns.
    fixture.stream.seek(target);

    // Blocks before the clip starts: silent, and the one that takes the cue up
    // is what lets the reader get ahead.
    for (auto block = 0; block < 4; ++block) {
        fixture.render(blockFrom(4.0 + static_cast<double>(block * kBlockSize) / kSampleRate));
        REQUIRE(fixture.at(0) == approx(0.0f));
    }

    // And then it starts, with the material already in memory.
    fixture.render(blockFrom(5.0));
    REQUIRE(fixture.at(0) == approx(static_cast<float>(target)));
    CHECK(fixture.stream.underruns() == 0);
}

TEST_CASE("Cueing a stream that is sounding does not interrupt it", "[engine][io][prefetch]") {
    // The loop-return case, from underneath: something cues the top of the loop
    // while the end of it is still playing. One reader cannot be in two places,
    // so what this owes is that playback is not damaged by being asked. The
    // cue waits; it does not abandon the material still going out.
    const ClipPlacement placement{{0.0, 10.0}, 0};
    Fixture fixture(placement);

    for (auto block = 0; block < 4; ++block)
        fixture.render(blockFrom(static_cast<double>(block * kBlockSize) / kSampleRate));

    const auto before = fixture.stream.underruns();
    fixture.stream.seek(fixture.source.sourceSampleAt(0.0));

    // Every block after the cue is the one that was going to play anyway,
    // sample for sample, with nothing dropped and nothing re-read.
    for (auto block = 4; block < 12; ++block) {
        fixture.render(blockFrom(static_cast<double>(block * kBlockSize) / kSampleRate));

        INFO("block " << block);
        REQUIRE(fixture.at(0) == approx(static_cast<float>(block * kBlockSize)));
        REQUIRE(fixture.at(kBlockSize - 1) == approx(static_cast<float>(block * kBlockSize + 63)));
    }

    // And it cost nothing to have asked.
    CHECK(fixture.stream.underruns() == before);

    SECTION("and is taken up once it falls silent") {
        // The clip stops sounding, so the stream is free to go where it was
        // pointed, and the material is there when playback returns to it.
        fixture.render(blockFrom(20.0));
        fixture.render(blockFrom(20.0 + static_cast<double>(kBlockSize) / kSampleRate));

        fixture.render(blockFrom(0.0, kBlockSize, false));
        REQUIRE(fixture.at(0) == approx(0.0f));
        REQUIRE(fixture.at(kBlockSize - 1) == approx(static_cast<float>(kBlockSize - 1)));
        CHECK(fixture.stream.underruns() == before);
    }
}

TEST_CASE("A clip rolling past the end of its file is not sounding", "[engine][io][prefetch]") {
    // Five thousand samples of material under a placement that runs for a
    // second. Past the material the source still asks every block, because the
    // clip is still placed there, and every ask comes back with nothing.
    const ClipPlacement placement{{0.0, 1.0}, 0};
    Fixture fixture(placement, 5000);

    for (auto block = 0; block < 100; ++block)
        fixture.render(blockFrom(static_cast<double>(block * kBlockSize) / kSampleRate));

    REQUIRE(fixture.at(0) == approx(0.0f));
    REQUIRE(fixture.stream.underruns() == 0);

    // A stream playing nothing is exactly when pointing it at its next use is
    // worth doing, so being asked for silence must not look like sounding.
    fixture.stream.seek(fixture.source.sourceSampleAt(0.0));

    // Two more blocks past the end: the first takes the cue up, the second
    // reads ahead for it.
    fixture.render(blockFrom(static_cast<double>(100 * kBlockSize) / kSampleRate));
    fixture.render(blockFrom(static_cast<double>(101 * kBlockSize) / kSampleRate));

    // And the material is there when playback returns to it.
    fixture.render(blockFrom(0.0, kBlockSize, false));
    REQUIRE(fixture.at(0) == approx(0.0f));
    REQUIRE(fixture.at(kBlockSize - 1) == approx(static_cast<float>(kBlockSize - 1)));
    CHECK(fixture.stream.underruns() == 0);
}
