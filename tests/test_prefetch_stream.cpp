#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>

#include "io/PrefetchStream.hpp"

using magda::engine::AudioFileReader;
using magda::engine::PrefetchSettings;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;

namespace {

constexpr int kBlockSize = 64;

/**
 * A file whose samples say where they came from: sample n of channel c reads
 * back as n + c/2. Anything the stream delivers can therefore be checked
 * against the position it was supposed to come from, which reading a fixture
 * off a disk would not give and which is the whole question here.
 */
class CountingReader final : public AudioFileReader {
  public:
    explicit CountingReader(std::int64_t length, double rate = 44100.0)
        : length_(length), rate_(rate) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }

    double sampleRate() const override {
        return rate_;
    }

    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        ++reads;
        lastStart = startSample;
        largestRequest = std::max(largestRequest, numSamples);

        // What the file has from here, saturating rather than wrapping. A
        // reader with no end reports int64's maximum for its length, and it is
        // asked for positions ahead of its first sample -- LoopingAudioFileReader
        // answers those by phase and pads what is in front of them -- so the
        // plain subtraction overflows for exactly the pair this file is here to
        // stand in for.
        //
        // Branched the way PrefetchStream::fill is, and for the same reason
        // rather than for symmetry: int64's maximum plus the position is only
        // representable while the position is negative, so the sign has to be
        // established before that sum is formed. A test double that reproduced
        // the overflow it exists to catch would be undefined behaviour on the
        // path this case was widened to cover.
        const auto remaining = [this, startSample]() -> std::int64_t {
            constexpr auto unbounded = std::numeric_limits<std::int64_t>::max();

            if (startSample >= 0)
                return length_ - startSample;  // both non-negative, so no wrap

            const auto headroom = unbounded + startSample;
            return length_ > headroom ? unbounded : length_ - startSample;
        }();

        const auto available = static_cast<int>(std::clamp<std::int64_t>(remaining, 0, numSamples));

        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < available; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      valueAt(startSample + sample, channel));

        if (available < numSamples)
            destination.clear(destinationOffset + available, numSamples - available);

        return available;
    }

    static float valueAt(std::int64_t sample, int channel) {
        return static_cast<float>(sample) + static_cast<float>(channel) * 0.5f;
    }

    int reads = 0;
    std::int64_t lastStart = -1;

    /// The largest run this reader was ever asked for. A stream must never ask
    /// for more than a chunk holds, and the one time it did, it did so by
    /// arithmetic rather than by intent.
    int largestRequest = 0;

  private:
    std::int64_t length_;
    double rate_;
};

RenderContext context() {
    return RenderContext{44100.0, kBlockSize, 2};
}

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

/// A stream over a counting file, with the reader kept to hand.
struct Fixture {
    explicit Fixture(std::int64_t length = 100000, PrefetchSettings settings = {256, 4}) {
        auto owned = std::make_unique<CountingReader>(length);
        reader = owned.get();
        stream = std::make_unique<PrefetchStream>(std::move(owned), context(), settings);
        output.setSize(2, kBlockSize);
        output.clear();
    }

    /// Point the reader at a position before anything plays from it, which is
    /// what whoever schedules the material does: a stream that is asked for a
    /// position it was never pointed at has to seek, and a seek costs a block.
    void cue(std::int64_t sourceStart) {
        stream->seek(sourceStart);

        // Stands in for the callback before this one: a cue is published from
        // another thread and taken up on the audio thread, which is what keeps
        // the cross-thread path away from everything the callback owns.
        stream->applyPendingCue();
    }

    /// Read one block's worth from a position, having read ahead first.
    int readPrefetched(std::int64_t sourceStart, int numSamples = kBlockSize) {
        while (stream->fill()) {
        }
        return read(sourceStart, numSamples);
    }

    int read(std::int64_t sourceStart, int numSamples = kBlockSize) {
        output.clear();
        return stream->read(sourceStart, juce::dsp::AudioBlock<float>(output), numSamples);
    }

    /// What the block holds, if it holds the file from this position.
    void requireHolds(std::int64_t sourceStart, int numSamples = kBlockSize) {
        for (auto sample = 0; sample < numSamples; ++sample) {
            INFO("sample " << sample);
            REQUIRE(output.getSample(0, sample) ==
                    approx(CountingReader::valueAt(sourceStart + sample, 0)));
            REQUIRE(output.getSample(1, sample) ==
                    approx(CountingReader::valueAt(sourceStart + sample, 1)));
        }
    }

    void requireSilent(int from, int to) {
        for (auto sample = from; sample < to; ++sample) {
            INFO("sample " << sample);
            REQUIRE(output.getSample(0, sample) == approx(0.0f));
        }
    }

    CountingReader* reader = nullptr;
    std::unique_ptr<PrefetchStream> stream;
    juce::AudioBuffer<float> output;
};

}  // namespace

TEST_CASE("A stream plays what was read ahead of it", "[engine][io][prefetch]") {
    Fixture fixture;

    SECTION("from the beginning, block after block") {
        for (auto block = 0; block < 20; ++block) {
            const auto position = static_cast<std::int64_t>(block) * kBlockSize;
            REQUIRE(fixture.readPrefetched(position) == kBlockSize);
            fixture.requireHolds(position);
        }

        // Playing straight through is not a seek, however many chunk
        // boundaries it crosses.
        CHECK(fixture.stream->underruns() == 0);
    }

    SECTION("across a chunk boundary in one block") {
        // The chunk is 256 samples; this block starts 32 short of the end of
        // the first one, so it is served out of two.
        fixture.cue(224);
        REQUIRE(fixture.readPrefetched(224) == kBlockSize);
        fixture.requireHolds(224);
    }

    SECTION("in blocks of different lengths") {
        REQUIRE(fixture.readPrefetched(0, 10) == 10);
        fixture.requireHolds(0, 10);

        REQUIRE(fixture.readPrefetched(10, 50) == 50);
        fixture.requireHolds(10, 50);
    }

    SECTION("with a pool small enough to have to be recycled") {
        // Two chunks of 64: eight blocks cannot be in flight at once, so this
        // only works if what the callback finishes with goes back to the
        // reader.
        Fixture small(100000, {64, 2});

        for (auto block = 0; block < 8; ++block) {
            const auto position = static_cast<std::int64_t>(block) * kBlockSize;
            REQUIRE(small.readPrefetched(position) == kBlockSize);
            small.requireHolds(position);
        }
    }
}

TEST_CASE("A reader that has not caught up renders silence and says so", "[engine][io][prefetch]") {
    Fixture fixture;

    SECTION("nothing read ahead is nothing to play") {
        CHECK(fixture.read(0) == 0);
        fixture.requireSilent(0, kBlockSize);
        CHECK(fixture.stream->underruns() == 1);
    }

    SECTION("and the gap does not become a delay") {
        // The first block is missed. The second is not the audio the first one
        // wanted arriving late: it is the audio the second one wanted, which
        // is what keeps a stream that ran dry once from staying behind for the
        // rest of the take.
        CHECK(fixture.read(0) == 0);

        REQUIRE(fixture.readPrefetched(kBlockSize) == kBlockSize);
        fixture.requireHolds(kBlockSize);
    }

    SECTION("the end of the file is not an underrun") {
        Fixture shortFile(100);

        REQUIRE(shortFile.readPrefetched(0, kBlockSize) == kBlockSize);
        shortFile.requireHolds(0);

        // Half of this block is past the last sample.
        CHECK(shortFile.readPrefetched(64, kBlockSize) == 36);
        shortFile.requireHolds(64, 36);
        shortFile.requireSilent(36, kBlockSize);

        // Entirely past it.
        CHECK(shortFile.readPrefetched(128, kBlockSize) == 0);
        shortFile.requireSilent(0, kBlockSize);

        CHECK(shortFile.stream->underruns() == 0);
    }
}

TEST_CASE("A seek drops what was read for somewhere else", "[engine][io][prefetch]") {
    Fixture fixture;

    // A full pool, all of it from the top of the file.
    while (fixture.stream->fill()) {
    }
    REQUIRE(fixture.reader->reads > 0);

    SECTION("the block that seeks plays silence rather than the wrong thing") {
        CHECK(fixture.read(50000) == 0);
        fixture.requireSilent(0, kBlockSize);
        CHECK(fixture.stream->underruns() == 1);
    }

    SECTION("and what arrives afterwards is from the new position") {
        fixture.read(50000);

        REQUIRE(fixture.readPrefetched(50000 + kBlockSize) == kBlockSize);
        fixture.requireHolds(50000 + kBlockSize);

        // Only the seek cost anything. Playing on from there does not.
        CHECK(fixture.stream->underruns() == 1);
    }

    SECTION("seeking backwards is the same as seeking forwards") {
        for (auto block = 0; block < 8; ++block)
            fixture.readPrefetched(static_cast<std::int64_t>(block) * kBlockSize);

        fixture.read(0);
        REQUIRE(fixture.readPrefetched(kBlockSize) == kBlockSize);
        fixture.requireHolds(kBlockSize);
    }
}

TEST_CASE("A stream asked for nothing does nothing", "[engine][io][prefetch]") {
    Fixture fixture;

    CHECK(fixture.readPrefetched(0, 0) == 0);
    CHECK(fixture.stream->underruns() == 0);
}

TEST_CASE("A reader that fails is not a reader that returned audio", "[engine][io][prefetch]") {
    // A decode can fail inside the length a file advertises: truncated under
    // us, a stream that went away mid-frame. What must not happen is the
    // destination's previous contents being reported as material.
    class FailingFormatReader final : public juce::AudioFormatReader {
      public:
        FailingFormatReader() : juce::AudioFormatReader(nullptr, "failing") {
            sampleRate = 44100.0;
            bitsPerSample = 32;
            numChannels = 2;
            usesFloatingPointData = true;
            lengthInSamples = 10000;
        }

        bool readSamples(int* const*, int, int, juce::int64, int) override {
            return false;
        }
    };

    magda::engine::JuceAudioFileReader reader(std::make_unique<FailingFormatReader>());

    juce::AudioBuffer<float> destination(2, kBlockSize);
    for (auto channel = 0; channel < 2; ++channel)
        for (auto sample = 0; sample < kBlockSize; ++sample)
            destination.setSample(channel, sample, 0.5f);

    CHECK(reader.read(destination, 0, 0, kBlockSize) == 0);

    // Cleared, so that nothing downstream can publish what was in the buffer
    // before as audio read from the file.
    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(destination.getSample(0, sample) == approx(0.0f));
    }
}

TEST_CASE("A source with no end is not read past the end of a chunk", "[engine][io][prefetch]") {
    // A looping reader has no last sample, so it reports int64's maximum as its
    // length (SourceReaders.cpp), and a position can be negative: an event
    // anchored before its own loop start is anchored at a phase within it, and
    // a reversed clip trimmed longer than its source begins ahead of its first
    // sample.
    //
    // How much is left was `length - position`, which overflows for exactly
    // that pair. The wrap is silent and it is not merely a wrong number: the
    // large negative it produces truncates back to a positive int larger than
    // a chunk, so the read writes past the end of the buffer it was handed. The
    // project that found it asked for 8414 samples into a chunk of 4096.
    //
    // Asserted as "never more than a chunk" rather than as one number, because
    // the bound is what makes the read safe and the number is an accident of
    // which position overflowed.
    const PrefetchSettings settings{256, 4};
    Fixture fixture(std::numeric_limits<std::int64_t>::max(), settings);

    const auto chunkSamples = std::max(settings.chunkSamples, kBlockSize);

    // Played across zero rather than up to it, and that is the point of the
    // loop. The pool holds four chunks, so one round of fill() from -8415
    // advances the read-ahead by a kilosample and never reaches a position that
    // is not negative -- which would leave the ordinary path untested, and the
    // ordinary path is where the second overflow lived: the saturating form
    // needs int64's maximum plus the position, and that sum is only
    // representable while the position is negative.
    fixture.cue(-8415);

    auto played = 0;
    std::int64_t lastPosition = 0;
    for (std::int64_t position = -8415; position < 2048; position += kBlockSize) {
        lastPosition = position;
        INFO("position " << position);
        played += fixture.readPrefetched(position);

        // Every round, not once at the end. A single request past the bound is
        // a write past the end of a chunk, whatever the rest of the run does.
        REQUIRE(fixture.reader->largestRequest > 0);
        REQUIRE(fixture.reader->largestRequest <= chunkSamples);
    }

    // And it played: the guard is a clamp, not a refusal. A position in front
    // of the first sample reads as silence, which is what a stream on its way
    // into its own material is, and the material itself follows.
    CHECK(played > 0);
    CHECK(lastPosition > 0);
    fixture.requireHolds(lastPosition);
}
