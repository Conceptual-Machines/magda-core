#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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

        const auto available =
            static_cast<int>(std::clamp<std::int64_t>(length_ - startSample, 0, numSamples));

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
