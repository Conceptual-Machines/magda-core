#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "ReaderGate.hpp"
#include "io/PrefetchStream.hpp"
#include "io/PrefetchThread.hpp"

/**
 * A prefetch stream whose reader is delayed on purpose (#2700). Pauses are
 * whole worker rounds withheld between callbacks, or one read held at a gate.
 */

using magda::engine::AudioFileReader;
using magda::engine::PrefetchSettings;
using magda::engine::PrefetchStream;
using magda::engine::PrefetchThread;
using magda::engine::ReadPurpose;
using magda::engine::RenderContext;
using Gate = magda::test::ReaderGate;
using Worker = magda::test::GatedWorker;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr std::int64_t kUnbounded = std::numeric_limits<std::int64_t>::max();

/// Frame n of channel c reads back as offset + n + 1 + c/4, so silence is never material.
class CountingReader final : public AudioFileReader {
  public:
    CountingReader(std::int64_t length, float offset, Gate* gate)
        : length_(length), offset_(offset), gate_(gate) {
        starts.reserve(4096);
    }

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t start,
             int numSamples) override {
        starts.push_back(start);
        if (gate_ != nullptr)
            gate_->pass(start);

        const auto available =
            length_ == kUnbounded
                ? numSamples
                : static_cast<int>(std::clamp<std::int64_t>(length_ - start, 0, numSamples));

        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < available; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      valueAt(start + sample, channel));

        return available;
    }

    float valueAt(std::int64_t frame, int channel) const {
        return offset_ + static_cast<float>(frame + 1) + 0.25f * static_cast<float>(channel);
    }

    /// Where each read began. Only inspected while no worker is running.
    std::vector<std::int64_t> starts;

  private:
    std::int64_t length_;
    float offset_;
    Gate* gate_;
};

struct Stream {
    Stream(int blockSizeIn, PrefetchSettings settings, std::int64_t length = 1000000,
           float offset = 0.0f, Gate* gate = nullptr)
        : blockSize(blockSizeIn),
          chunkSamples(std::max(settings.chunkSamples, blockSizeIn)),
          coverage(static_cast<std::int64_t>(chunkSamples) * settings.chunkCount) {
        auto owned = std::make_unique<CountingReader>(length, offset, gate);
        reader = owned.get();
        stream = std::make_unique<PrefetchStream>(
            std::move(owned), RenderContext{kSampleRate, blockSize, 2}, settings);
        output.setSize(2, blockSize);
    }

    /// One worker round.
    void fill() {
        stream->fill();
    }

    int read(std::int64_t start, ReadPurpose purpose = ReadPurpose::playback) {
        output.clear();
        return stream->read(start, juce::dsp::AudioBlock<float>(output), blockSize, purpose);
    }

    /// Whether the first @p count frames are the file from @p start, and the rest silent.
    bool holds(std::int64_t start, int count) const {
        for (auto sample = 0; sample < blockSize; ++sample)
            for (auto channel = 0; channel < 2; ++channel) {
                const auto expected =
                    sample < count ? reader->valueAt(start + sample, channel) : 0.0f;
                if (output.getSample(channel, sample) != expected)
                    return false;
            }
        return true;
    }

    std::int64_t missing(ReadPurpose purpose = ReadPurpose::playback) const {
        return stream->missingFrames(purpose);
    }

    int blockSize;
    int chunkSamples;
    std::int64_t coverage;
    CountingReader* reader = nullptr;
    std::unique_ptr<PrefetchStream> stream;
    juce::AudioBuffer<float> output;
};

std::int64_t readsBefore(const CountingReader& reader, std::int64_t position) {
    return std::ranges::count_if(reader.starts, [&](auto start) { return start < position; });
}

}  // namespace

TEST_CASE("Missing frames are counted per source frame, apart from short reads",
          "[engine][io][prefetch][2700]") {
    SECTION("a stereo read that finds nothing misses its frames once") {
        Stream stream(64, {256, 4});
        CHECK(stream.read(0) == 0);
        CHECK(stream.missing() == 64);
        CHECK(stream.missing(ReadPurpose::priming) == 0);
        CHECK(stream.stream->underruns() == 1);
    }

    SECTION("priming is attributed separately") {
        Stream stream(64, {256, 4});
        stream.read(0, ReadPurpose::priming);
        CHECK(stream.missing(ReadPurpose::priming) == 64);
        CHECK(stream.missing() == 0);
    }

    SECTION("frames past the end are not missing") {
        Stream stream(64, {256, 4}, 100);
        CHECK(stream.read(64) == 0);
        CHECK(stream.missing() == 36);
        CHECK(stream.read(128) == 0);
        CHECK(stream.missing() == 36);
    }

    SECTION("frames before sample zero pad a bounded reading") {
        Stream stream(64, {256, 4});
        stream.read(-40);
        CHECK(stream.missing() == 24);
    }

    SECTION("and are material in a looping one") {
        Stream stream(64, {256, 4}, kUnbounded);
        stream.read(-40);
        CHECK(stream.missing() == 64);
    }
}

TEST_CASE("Sequential playback survives a stall while resident audio lasts",
          "[engine][io][prefetch][2700]") {
    // Chunks of 300 put the end of coverage between block boundaries.
    const PrefetchSettings settings{300, 4};

    for (const auto blockSize : {64, 128, 512}) {
        const Stream probe(blockSize, settings);
        const auto complete = static_cast<int>(probe.coverage / blockSize);

        for (const auto stallBlocks : {complete, complete + 1, 3 * complete + 5}) {
            INFO("block " << blockSize << ", stalled callbacks " << stallBlocks);
            Stream stream(blockSize, settings);
            stream.fill();

            std::int64_t position = 0;
            auto shortReads = 0;
            for (auto block = 0; block < stallBlocks; ++block, position += blockSize) {
                const auto expected = static_cast<int>(
                    std::clamp<std::int64_t>(stream.coverage - position, 0, blockSize));
                REQUIRE(stream.read(position) == expected);
                REQUIRE(stream.holds(position, expected));
                shortReads += expected < blockSize ? 1 : 0;
            }

            const auto lost = std::max<std::int64_t>(0, position - stream.coverage);
            CHECK(stream.missing() == lost);
            CHECK(stream.stream->underruns() == shortReads);

            // One worker round per callback from here. Each round refills the
            // whole pool from where the last one stopped, behind the cursor.
            const auto behind = position - stream.coverage;
            const auto expectedRounds =
                behind <= 0 ? 1
                            : static_cast<int>((behind + stream.coverage - blockSize - 1) /
                                               (stream.coverage - blockSize));
            auto rounds = 0;
            for (;; position += blockSize) {
                stream.fill();
                ++rounds;
                const auto delivered = stream.read(position);
                REQUIRE(stream.holds(position, delivered));
                if (delivered == blockSize)
                    break;
                REQUIRE(rounds < 64);
            }
            CHECK(rounds == expectedRounds);

            // No lasting shift once it has caught up.
            for (auto block = 1; block < 16; ++block) {
                stream.fill();
                REQUIRE(stream.read(position + block * blockSize) == blockSize);
                REQUIRE(stream.holds(position + block * blockSize, blockSize));
            }
        }
    }
}

TEST_CASE("A read held in flight delays only what lies behind it", "[engine][io][prefetch][2700]") {
    Gate gate;
    Stream stream(64, {256, 4}, 1000000, 0.0f, &gate);
    gate.closeFrom(512);

    std::int64_t position = 0;
    {
        Worker worker(gate, [&] { stream.fill(); });
        REQUIRE(gate.waitUntilHeld());

        // Two chunks landed before the held read.
        for (; position < 768; position += 64) {
            const auto expected =
                static_cast<int>(std::clamp<std::int64_t>(512 - position, 0, stream.blockSize));
            REQUIRE(stream.read(position) == expected);
            REQUIRE(stream.holds(position, expected));
        }
        CHECK(stream.missing() == 768 - 512);
    }

    REQUIRE(stream.read(position) == 64);
    CHECK(stream.holds(position, 64));
    CHECK(stream.missing() == 768 - 512);
}

TEST_CASE("A seek during an in-flight fill waits for the worker to finish the old pool",
          "[engine][io][prefetch][2700]") {
    Gate gate;
    Stream stream(64, {256, 4}, 1000000, 0.0f, &gate);
    gate.closeFrom(256);

    constexpr std::int64_t kTarget = 50000;
    {
        Worker worker(gate, [&] { stream.fill(); });
        REQUIRE(gate.waitUntilHeld());

        REQUIRE(stream.read(0) == 64);
        CHECK(stream.read(kTarget) == 0);
    }

    // The held read and three more chunks, all for the position abandoned.
    CHECK(readsBefore(*stream.reader, kTarget) == 5);
    CHECK(std::ssize(stream.reader->starts) == 5);

    CHECK(stream.read(kTarget + 64) == 0);
    stream.fill();
    REQUIRE(stream.read(kTarget + 128) == 64);
    CHECK(stream.holds(kTarget + 128, 64));
    CHECK(stream.missing() == 128);
}

TEST_CASE("A held read starves the streams the worker has not reached",
          "[engine][io][prefetch][2700]") {
    constexpr int kStreams = 3;
    constexpr int kBlocks = 6;

    for (auto held = 0; held < kStreams; ++held) {
        INFO("held stream " << held);
        Gate gate;
        std::vector<std::unique_ptr<Stream>> streams;
        for (auto index = 0; index < kStreams; ++index)
            streams.push_back(std::make_unique<Stream>(64, PrefetchSettings{256, 4}, 1000000,
                                                       100000.0f * static_cast<float>(index),
                                                       index == held ? &gate : nullptr));

        PrefetchThread reader(false);
        for (auto& stream : streams)
            reader.add(*stream->stream);

        gate.closeFrom(256);
        {
            Worker worker(gate, [&] { reader.fillOnce(); });
            REQUIRE(gate.waitUntilHeld());

            for (auto block = 0; block < kBlocks; ++block)
                for (auto& stream : streams) {
                    const auto delivered = stream->read(block * 64);
                    REQUIRE(stream->holds(block * 64, delivered));
                }

            for (auto index = 0; index < kStreams; ++index) {
                INFO("stream " << index);
                const auto resident = index < held ? 1024 : (index == held ? 256 : 0);
                CHECK(streams[static_cast<std::size_t>(index)]->missing() ==
                      std::max(0, kBlocks * 64 - resident));
            }
        }

        for (auto& stream : streams) {
            REQUIRE(stream->read(kBlocks * 64) == 64);
            CHECK(stream->holds(kBlocks * 64, 64));
        }

        for (auto& stream : streams)
            reader.remove(*stream->stream);
    }
}

TEST_CASE("A playing locate to unread audio costs the callbacks until the reader answers",
          "[engine][io][prefetch][2700]") {
    for (const auto withheld : {0, 1, 3}) {
        for (const auto target : std::vector<std::int64_t>{50000, 100}) {
            INFO("withheld rounds " << withheld << ", target " << target);
            Stream stream(64, {256, 4});

            std::int64_t position = 0;
            for (; position < 3000; position += 64) {
                stream.fill();
                REQUIRE(stream.read(position) == 64);
            }

            for (auto block = 0; block <= withheld; ++block)
                CHECK(stream.read(target + block * 64) == 0);

            stream.fill();
            const auto recovered = target + (withheld + 1) * 64;
            REQUIRE(stream.read(recovered) == 64);
            CHECK(stream.holds(recovered, 64));
            CHECK(stream.missing() == (withheld + 1) * 64);
        }
    }
}

TEST_CASE("A playing locate inside resident audio does not wait for the reader",
          "[engine][io][prefetch][2700][!shouldfail]") {
    // Finding 1 of docs/issues/2699-streaming-seek-audit.md: a discontinuity
    // drops the pool even when the destination is already in it.
    Stream stream(64, {256, 4});
    stream.fill();
    REQUIRE(stream.read(0) == 64);

    CHECK(stream.read(512) == 64);
    stream.fill();
    CHECK(stream.read(576) == 64);
    CHECK(stream.missing() == 0);
    CHECK(readsBefore(*stream.reader, 1024) == 4);
}
