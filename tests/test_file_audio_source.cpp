#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "io/FileAudioSource.hpp"
#include "io/StreamingAudioSource.hpp"

using magda::engine::AudioFileReader;
using magda::engine::BlockInfo;
using magda::engine::ClipPlacement;
using magda::engine::FileAudioSource;
using magda::engine::PrefetchSettings;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
using magda::engine::StreamingAudioSource;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;

/// Sample n reads back as n, so where a sample came from is readable off its
/// value rather than inferred from where it landed.
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

BlockInfo blockFrom(double startSeconds, int numSamples = kBlockSize, bool continuous = true) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startSeconds = startSeconds;
    block.endSeconds = startSeconds + numSamples / kSampleRate;
    block.startBeat = startSeconds * 2.0;
    block.endBeat = block.endSeconds * 2.0;
    block.continuous = continuous;
    return block;
}

/// The whole of channel 0 across @p numBlocks consecutive blocks, so what a
/// source rendered is one stream rather than a pile of buffers.
std::vector<float> renderStream(magda::engine::EngineAudioSource& source, double startSeconds,
                                int numBlocks, int blockSize = kBlockSize) {
    juce::AudioBuffer<float> buffer(2, blockSize);
    std::vector<float> stream;

    for (auto index = 0; index < numBlocks; ++index) {
        const auto seconds = startSeconds + index * blockSize / kSampleRate;
        source.render(blockFrom(seconds, blockSize, index > 0),
                      juce::dsp::AudioBlock<float>(buffer));
        for (auto sample = 0; sample < blockSize; ++sample)
            stream.push_back(buffer.getSample(0, sample));
    }

    return stream;
}

}  // namespace

TEST_CASE("An offline source plays the samples the clip is placed over", "[engine][io][offline]") {
    CountingReader reader(100000);
    const ClipPlacement placement{0.0, 1.0, 0};
    FileAudioSource source(reader, placement);
    source.prepare(context());

    const auto stream = renderStream(source, 0.0, 3);

    REQUIRE(stream.size() == static_cast<std::size_t>(3 * kBlockSize));
    CHECK(stream[0] == approx(0.0f));
    CHECK(stream[1] == approx(1.0f));
    CHECK(stream[kBlockSize] == approx(static_cast<float>(kBlockSize)));
    CHECK(stream.back() == approx(static_cast<float>(3 * kBlockSize - 1)));
}

TEST_CASE("An offline source and a streaming one render the same clip identically",
          "[engine][io][offline]") {
    // The claim the whole offline path rests on: a bounce is what was heard.
    // The two read through different machinery and share only the placement
    // mapping, so this is what catches them drifting apart.
    const ClipPlacement placement{0.5, 2.0, 1000};

    // Starting shortly before the clip, so the comparison covers the silence in
    // front of it, the block its start falls inside, and material either side
    // of a chunk boundary. Starting on it would compare two runs of silence and
    // pass whatever the two sources did with a file.
    constexpr double kFirstBlockSeconds = 0.49;
    constexpr int kBlocks = 40;

    CountingReader offlineReader(100000);
    FileAudioSource offline(offlineReader, placement);
    offline.prepare(context());
    const auto offlineStream = renderStream(offline, kFirstBlockSeconds, kBlocks);

    PrefetchStream stream(std::make_unique<CountingReader>(100000), context(),
                          PrefetchSettings{256, 8});
    StreamingAudioSource live(stream, placement);
    live.prepare(context());

    // No prefetch thread: fill() is pumped here instead, so the live source is
    // never short and the comparison is about the samples rather than about a
    // disk's timing. Cued and taken up before the first block, which is what
    // scheduling material ahead of the callback amounts to.
    stream.seek(live.sourceSampleAt(placement.startSeconds));
    stream.applyPendingCue();

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::vector<float> liveStream;

    for (auto index = 0; index < kBlocks; ++index) {
        while (stream.fill()) {
        }

        const auto seconds = kFirstBlockSeconds + index * kBlockSize / kSampleRate;
        live.render(blockFrom(seconds, kBlockSize, index > 0),
                    juce::dsp::AudioBlock<float>(buffer));
        for (auto sample = 0; sample < kBlockSize; ++sample)
            liveStream.push_back(buffer.getSample(0, sample));
    }

    CHECK(stream.underruns() == 0);

    // The comparison has to be over material. Two runs of silence agree
    // perfectly and say nothing, which is what this test did before the range
    // was moved onto the clip.
    const auto sounding = std::count_if(offlineStream.begin(), offlineStream.end(),
                                        [](float sample) { return sample != 0.0f; });
    REQUIRE(sounding > kBlockSize);

    REQUIRE(liveStream.size() == offlineStream.size());
    for (std::size_t sample = 0; sample < offlineStream.size(); ++sample) {
        INFO("sample " << sample);
        REQUIRE(liveStream[sample] == approx(offlineStream[sample]));
    }
}

TEST_CASE("An offline source renders the same samples at any block size", "[engine][io][offline]") {
    const ClipPlacement placement{0.0, 4.0, 0};

    CountingReader shortReader(100000);
    FileAudioSource shortBlocks(shortReader, placement);
    shortBlocks.prepare(RenderContext{kSampleRate, 16, 2});

    CountingReader longReader(100000);
    FileAudioSource longBlocks(longReader, placement);
    longBlocks.prepare(RenderContext{kSampleRate, 256, 2});

    const auto fromShort = renderStream(shortBlocks, 0.0, 16, 16);
    const auto fromLong = renderStream(longBlocks, 0.0, 1, 256);

    REQUIRE(fromShort.size() == fromLong.size());
    for (std::size_t sample = 0; sample < fromShort.size(); ++sample) {
        INFO("sample " << sample);
        REQUIRE(fromShort[sample] == approx(fromLong[sample]));
    }
}

TEST_CASE("An offline source renders silence outside the clip", "[engine][io][offline]") {
    CountingReader reader(100000);

    // The clip starts a quarter of a block in and ends half a block later.
    const auto quarter = 0.25 * kBlockSize / kSampleRate;
    const ClipPlacement placement{quarter, quarter + 0.5 * kBlockSize / kSampleRate, 0};
    FileAudioSource source(reader, placement);
    source.prepare(context());

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    source.render(blockFrom(0.0), juce::dsp::AudioBlock<float>(buffer));

    CHECK(buffer.getSample(0, 0) == approx(0.0f));
    CHECK(buffer.getSample(0, kBlockSize / 4) == approx(0.0f));
    CHECK(buffer.getSample(0, kBlockSize / 4 + 1) == approx(1.0f));
    CHECK(buffer.getSample(0, 3 * kBlockSize / 4) == approx(0.0f));
    CHECK(buffer.getSample(0, kBlockSize - 1) == approx(0.0f));
}

TEST_CASE("An offline source renders nothing while the transport is stopped",
          "[engine][io][offline]") {
    CountingReader reader(100000);
    FileAudioSource source(reader, ClipPlacement{0.0, 4.0, 0});
    source.prepare(context());

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    auto block = blockFrom(1.0);
    block.playing = false;
    block.endSeconds = block.startSeconds;

    source.render(block, juce::dsp::AudioBlock<float>(buffer));

    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(buffer.getSample(0, sample) == approx(0.0f));
    }
}

TEST_CASE("An offline source runs off the end of a file as silence", "[engine][io][offline]") {
    // The clip claims more material than the file has. What is missing is
    // silence, and the clip stays the length it was placed at: a file that ran
    // out is not a reason to move what comes after it.
    CountingReader reader(kBlockSize + 10);
    FileAudioSource source(reader, ClipPlacement{0.0, 4.0, 0});
    source.prepare(context());

    const auto stream = renderStream(source, 0.0, 2);

    CHECK(stream[kBlockSize + 9] == approx(static_cast<float>(kBlockSize + 9)));
    CHECK(stream[kBlockSize + 10] == approx(0.0f));
    CHECK(stream.back() == approx(0.0f));
}
