#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "clip/ClipAudioSource.hpp"
#include "clip/EventPlacement.hpp"
#include "clip/FadeCurves.hpp"
#include "io/ClipPlacement.hpp"

/**
 * What a track's audio clips sum to (#2035), on the audio thread.
 *
 * The streams are hand-built here rather than provisioned: what a voice does
 * with a reader that is already standing by is this file's question, and where
 * that reader came from is test_clip_voice_pool.cpp's.
 *
 * Everything rolls. A test that skipped from one block to a distant one would
 * be testing a locate rather than playback, because a read that does not
 * continue the last one is a seek and costs a block of silence (#2016). So the
 * rig cues its readers where the transport is about to be, runs the blocks in
 * between, and probes the one it cares about.
 *
 * Times are whole blocks, so a probe lands on a block boundary and a sample of
 * the counting reader can be named exactly.
 */

using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::BlockInfo;
using magda::engine::ClipAudioSource;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSnapshotFeed;
using magda::engine::ClipStreamFeed;
using magda::engine::ClipStreamTable;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
using magda::engine::SnapshotSpan;
using magda::engine::TrackClipPlayback;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
constexpr magda::TrackId kTrack = 7;

/// When the block with this index begins.
double blockTime(int index) {
    return index * static_cast<double>(kBlockSize) / kSampleRate;
}

/// Sample n reads back as n, so where a sample came from is readable off the
/// value and a position that slipped is visible rather than inferred.
class CountingReader final : public magda::engine::AudioFileReader {
  public:
    explicit CountingReader(std::int64_t length = 10000000) : length_(length) {}

    std::int64_t lengthInSamples() const override {
        return length_;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    /// Ends where it says it does, the way the real reader does. A clip that
    /// reads past its own source has to find silence there rather than more
    /// counting, which is the difference between the tests below meaning
    /// anything and not.
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        const auto available =
            static_cast<int>(std::clamp<std::int64_t>(length_ - startSample, 0, numSamples));

        if (available < numSamples)
            destination.clear(destinationOffset + available, numSamples - available);
        if (available <= 0)
            return 0;

        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < available; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample));

        return available;
    }

  private:
    std::int64_t length_ = 0;
};

/// Every sample the same, so a gain, a fade or a pan is readable straight off
/// the output without having to divide by what the material was doing.
class ConstantReader final : public magda::engine::AudioFileReader {
  public:
    explicit ConstantReader(float value = 1.0f) : value_(value) {}

    std::int64_t lengthInSamples() const override {
        return 10000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample, value_);
        return numSamples;
    }

  private:
    float value_;
};

/// A different value on each side, so which of the source's channels a sample
/// came from is readable off it.
class SidesReader final : public magda::engine::AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 10000000;
    }
    double sampleRate() const override {
        return kSampleRate;
    }
    int numChannels() const override {
        return 2;
    }

    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      channel == 0 ? 1.0f : 2.0f);
        return numSamples;
    }
};

RenderContext context() {
    return RenderContext{kSampleRate, kBlockSize, 2};
}

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

/// A span from its seconds. The beats ride along at 120 bpm and nothing on the
/// audio path reads them: a source is handed seconds (ClipPlacement.hpp).
SnapshotSpan seconds(double start, double end) {
    SnapshotSpan span;
    span.startBeat = start * 2.0;
    span.endBeat = end * 2.0;
    span.startSeconds = start;
    span.endSeconds = end;
    return span;
}

SnapshotSpan blocks(int firstBlock, int lastBlock) {
    return seconds(blockTime(firstBlock), blockTime(lastBlock));
}

BlockInfo blockFrom(double startSeconds, bool continuous = true) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;
    block.seconds.start = startSeconds;
    block.seconds.end = startSeconds + kBlockSize / kSampleRate;
    block.beats.start = startSeconds * 2.0;
    block.beats.end = block.seconds.end * 2.0;
    block.continuous = continuous;
    return block;
}

AudioClipPlayback clipOver(magda::ClipId id, SnapshotSpan span, std::int64_t anchor = 0) {
    AudioClipPlayback clip;
    clip.clipId = id;
    clip.span = span;
    clip.launchFadeSamples = 0;  // the ramp is its own test

    AudioEventPlayback event;
    event.eventId = id;
    event.sourceId = id;
    event.filePath = "take.wav";
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 1000.0;
    event.span = span;
    event.anchorSamples = anchor;
    clip.events.push_back(std::move(event));

    return clip;
}

/// A track's clips, the readers behind them, and a transport that rolls.
struct Rig {
    Rig() {
        source.prepare(context());
        output.setSize(2, kBlockSize);
        output.clear();
    }

    /// A reader standing by for one entry. Returned so a test can read its
    /// underrun count.
    ///
    /// Through the reading its event asked for, because that is what the pool
    /// hands a voice: reverse, looping and rate conversion are layers over the
    /// file rather than anything a voice does (io/SourceReaders.hpp). The clip
    /// has to be on the lane before its reader is given, which every test here
    /// does anyway.
    PrefetchStream& give(magda::ClipId clipId, magda::EventId eventId,
                         std::unique_ptr<magda::engine::AudioFileReader> reader,
                         magda::engine::PrefetchSettings settings = {256, 8}) {
        const auto* event = eventOf(clipId, eventId);
        REQUIRE(event != nullptr);

        auto stream = std::make_shared<PrefetchStream>(
            magda::engine::readThrough(std::move(reader),
                                       magda::engine::sourceReadFor(*event, kSampleRate)),
            context(), settings);
        auto& created = *stream;
        table.entries.push_back(ClipStreamTable::Entry{kTrack, clipId, eventId, std::move(stream)});
        return created;
    }

    void publish() {
        publish(lane);
    }

    void publish(const TrackClipPlayback& track) {
        auto compiled = std::make_shared<ClipSnapshot>();
        compiled->tracks.push_back(track);
        clips.publish(std::move(compiled));

        // Sorted the way ClipStreamTable::rangeFor searches it.
        std::sort(table.entries.begin(), table.entries.end(),
                  [](const ClipStreamTable::Entry& a, const ClipStreamTable::Entry& b) {
                      if (a.trackId != b.trackId)
                          return a.trackId < b.trackId;
                      if (a.clipId != b.clipId)
                          return a.clipId < b.clipId;
                      return a.eventId < b.eventId;
                  });
        streams.publish(std::make_shared<const ClipStreamTable>(table));
    }

    /**
     * @brief Put the transport @p lead blocks before @p targetBlock and roll to it.
     *
     * The cue is where the transport is about to be, which is what the pool
     * does, and the block it is given in is where the callback hands it over.
     * The output is left holding the block that starts at @p targetBlock.
     */
    void start(int targetBlock, int lead) {
        next_ = targetBlock - lead;

        for (const auto& entry : table.entries) {
            const auto* event = eventOf(entry.clipId, entry.eventId);
            REQUIRE(event != nullptr);
            entry.stream->seek(magda::engine::sourceSampleAt(
                magda::engine::placementFor(*event, kSampleRate),
                std::max(blockTime(next_), event->span.startSeconds), kSampleRate));
        }

        advance(lead + 1);
    }

    /// The next block, and the one after. Leaves the output holding the last.
    void advance(int count = 1) {
        for (auto index = 0; index < count; ++index) {
            render(blockFrom(blockTime(next_)));
            ++next_;
        }
    }

    void render(const BlockInfo& block) {
        if (autoFill)
            fill();
        source.render(block, juce::dsp::AudioBlock<float>(output));
    }

    void fill() {
        auto worked = true;
        while (worked) {
            worked = false;
            for (const auto& entry : table.entries)
                worked = entry.stream->fill() || worked;
        }
    }

    float at(int sample, int channel = 0) const {
        return output.getSample(channel, sample);
    }

    const AudioEventPlayback* eventOf(magda::ClipId clipId, magda::EventId eventId) const {
        for (const auto& clip : lane.audio)
            if (clip.clipId == clipId)
                for (const auto& event : clip.events)
                    if (event.eventId == eventId)
                        return &event;
        return nullptr;
    }

    TrackClipPlayback lane{kTrack, {}, {}};
    ClipSnapshotFeed clips;
    ClipStreamFeed streams;
    ClipAudioSource source{kTrack, clips, streams};
    ClipStreamTable table;
    juce::AudioBuffer<float> output;

    /// Off, a test can run the callback further than the reader has got, which
    /// is how a block that comes back half filled is arranged on purpose.
    bool autoFill = true;

    int next_ = 0;
};

}  // namespace

TEST_CASE("A track plays the clips the snapshot placed on it", "[engine][clip][voice]") {
    // Blocks 100 to 300 of the timeline, playing the file from its tenth sample.
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(100, 300), 10));
    rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    SECTION("nothing before it starts") {
        rig.start(50, 40);
        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(rig.at(sample) == approx(0.0f));
    }

    SECTION("the file from where it was trimmed, from the block it starts in") {
        rig.start(100, 40);
        for (auto sample = 0; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.at(sample) == approx(static_cast<float>(10 + sample)));
        }
    }

    SECTION("in step with the timeline, block after block") {
        rig.start(100, 40);
        for (auto block = 0; block < 40; ++block) {
            INFO("block " << block);
            REQUIRE(rig.at(0) == approx(static_cast<float>(10 + block * kBlockSize)));
            rig.advance();
        }
    }

    SECTION("and nothing once the span is over") {
        rig.start(300, 40);
        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(rig.at(sample) == approx(0.0f));
    }

    SECTION("a stopped transport is no material rather than a held sample") {
        rig.start(150, 40);
        REQUIRE(rig.at(0) != approx(0.0f));

        auto stopped = blockFrom(blockTime(200));
        stopped.playing = false;
        stopped.seconds.end = stopped.seconds.start;
        stopped.beats.end = stopped.beats.start;
        rig.render(stopped);

        for (auto sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(rig.at(sample) == approx(0.0f));
    }
}

TEST_CASE("A clip cued before it starts plays without a gap", "[engine][clip][voice]") {
    // The reason the pool runs ahead of the transport. Nothing reads this
    // stream until the clip starts, so the cue has to reach it through the
    // blocks where it plays nothing.
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(200, 400), 500));
    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    rig.start(200, 60);

    REQUIRE(rig.at(0) == approx(500.0f));
    REQUIRE(rig.at(kBlockSize - 1) == approx(static_cast<float>(500 + kBlockSize - 1)));
    CHECK(stream.underruns() == 0);
}

TEST_CASE("Overlapping clips sum, because the snapshot already decided they both play",
          "[engine][clip][voice]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(0, 400)));
    rig.lane.audio.push_back(clipOver(2, blocks(200, 600)));

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.give(2, 2, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    SECTION("one clip alone is itself") {
        rig.start(100, 60);
        REQUIRE(rig.at(0) == approx(1.0f));
    }

    SECTION("two across the overlap are both") {
        rig.start(300, 60);
        REQUIRE(rig.at(0) == approx(2.0f));
    }

    SECTION("and one again once the first has ended") {
        rig.start(500, 60);
        REQUIRE(rig.at(0) == approx(1.0f));
    }
}

TEST_CASE("An interior silence punches a hole without moving the reader", "[engine][clip][voice]") {
    // A clip dropped inside another leaves a hole in the middle of this one
    // (#2003). What plays after the hole is what would have played had it not
    // been there: the material goes on running underneath, muted.
    Rig rig;

    auto clip = clipOver(1, blocks(100, 1000));
    clip.silenced.push_back(blocks(400, 700));
    rig.lane.audio.push_back(clip);

    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    // Rolled into from before the clip, so the reader is already ahead and the
    // only thing that could cost an underrun here is the hole.
    rig.start(250, 250);
    REQUIRE(rig.at(0) == approx(static_cast<float>((250 - 100) * kBlockSize)));

    rig.advance(300);  // block 550, inside the hole
    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(0.0f));
    }

    rig.advance(300);  // block 850, past it
    REQUIRE(rig.at(0) == approx(static_cast<float>((850 - 100) * kBlockSize)));

    // Which is the whole point: reading straight across the hole is what keeps
    // this from being a seek on the far side of it.
    CHECK(stream.underruns() == 0);
}

TEST_CASE("A hole that opens mid-block cuts exactly where it starts", "[engine][clip][voice]") {
    Rig rig;

    auto clip = clipOver(1, blocks(0, 400));
    clip.silenced.push_back(seconds(blockTime(200) + 20.0 / kSampleRate, blockTime(300)));
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    rig.start(200, 100);

    for (auto sample = 0; sample < 20; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(1.0f));
    }
    for (auto sample = 20; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(0.0f));
    }
}

TEST_CASE("The resolved fades shape the edges of the audible span", "[engine][clip][voice]") {
    // The span, not the placement. What the lane leaves audible is what a
    // listener hears begin and end, and it is the only edge the snapshot gives.
    Rig rig;

    auto clip = clipOver(1, blocks(100, 2100));
    clip.fadeInSeconds = blockTime(400);
    clip.fadeOutSeconds = blockTime(400);
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    SECTION("it comes in from nothing") {
        rig.start(100, 100);
        REQUIRE(rig.at(0) == approx(0.0f));
        REQUIRE(rig.at(kBlockSize - 1) ==
                approx(static_cast<float>(kBlockSize - 1) / (400.0f * kBlockSize)));
    }

    SECTION("half way up at half way along") {
        rig.start(300, 300);
        REQUIRE(rig.at(0) == approx(0.5f));
    }

    SECTION("flat once it is in") {
        rig.start(700, 300);
        REQUIRE(rig.at(0) == approx(1.0f));
    }

    SECTION("and half way back down at half way through the fade out") {
        rig.start(1900, 300);
        REQUIRE(rig.at(0) == approx(0.5f));
    }

    SECTION("a curve is the shape it names") {
        auto curved = rig.lane;
        curved.audio[0].fadeInCurve = magda::FadeCurve::Convex;
        rig.publish(curved);

        rig.start(300, 300);
        REQUIRE(rig.at(0) == approx(magda::engine::fadeGain(magda::FadeCurve::Convex, 0.5f)));
    }
}

TEST_CASE("A clip plays at its own gain and pan", "[engine][clip][voice]") {
    Rig rig;

    auto clip = clipOver(1, blocks(0, 1000));
    clip.gainDb = -6.0f;
    clip.pan = 0.5f;
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    rig.start(300, 100);

    // The incumbent's law: linear, and hotter on one side rather than quieter
    // on the other, because a bounce has to match what was heard.
    const auto gain = juce::Decibels::decibelsToGain(-6.0f);
    REQUIRE(rig.at(0, 0) == approx(gain - 0.5f * gain));
    REQUIRE(rig.at(0, 1) == approx(gain + 0.5f * gain));
}

TEST_CASE("An event's own trim sits under the clip's gain", "[engine][clip][voice]") {
    // Under it rather than in place of it, so one event of several can be
    // levelled against the others without touching what the clip plays at.
    Rig rig;

    auto clip = clipOver(1, blocks(0, 1000));
    clip.gainDb = -6.0f;
    clip.events[0].gainDb = -6.0f;
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    rig.start(300, 100);

    REQUIRE(rig.at(0) == approx(juce::Decibels::decibelsToGain(-12.0f)));
}

TEST_CASE("A channel that is off is not heard, and the other one is heard on both",
          "[engine][clip][voice]") {
    // Not hard panned. Where a clip sits in the image is what its pan decides,
    // and silencing an output here would spend that decision on this.
    Rig rig;

    rig.lane.audio.push_back(clipOver(1, blocks(0, 1000)));
    rig.give(1, 1, std::make_unique<SidesReader>());
    rig.publish();

    SECTION("both on is the source as it is") {
        rig.start(300, 100);
        CHECK(rig.at(0, 0) == approx(1.0f));
        CHECK(rig.at(0, 1) == approx(2.0f));
    }

    SECTION("the left off leaves the right on both sides") {
        auto lane = rig.lane;
        lane.audio[0].events[0].leftChannelActive = false;
        rig.publish(lane);

        rig.start(300, 100);
        CHECK(rig.at(0, 0) == approx(2.0f));
        CHECK(rig.at(0, 1) == approx(2.0f));
    }

    SECTION("and the right off leaves the left on both") {
        auto lane = rig.lane;
        lane.audio[0].events[0].rightChannelActive = false;
        rig.publish(lane);

        rig.start(300, 100);
        CHECK(rig.at(0, 0) == approx(1.0f));
        CHECK(rig.at(0, 1) == approx(1.0f));
    }

    SECTION("neither is silence rather than the file") {
        auto lane = rig.lane;
        lane.audio[0].events[0].leftChannelActive = false;
        lane.audio[0].events[0].rightChannelActive = false;
        rig.publish(lane);

        rig.start(300, 100);
        CHECK(rig.at(0, 0) == approx(0.0f));
        CHECK(rig.at(0, 1) == approx(0.0f));
    }
}

TEST_CASE("A file recorded at another rate plays at the device's", "[engine][clip][voice]") {
    // Half the device's rate, so half a source sample per output sample. The
    // conversion is underneath (io/SourceReaders.hpp): the voice still consumes
    // one sample of the reading per output sample, and the clip plays at its
    // own pitch over its own length instead of at four semitones and half the
    // length it was placed at.
    Rig rig;

    auto clip = clipOver(1, blocks(100, 300), 10);
    clip.events[0].sourceSampleRate = kSampleRate / 2.0;
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    rig.start(100, 40);

    // From the source sample the trim names, because an anchor is a count at
    // the source's own rate and stays one.
    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(10.0f + 0.5f * static_cast<float>(sample)));
    }

    SECTION("and goes on being in step a hundred blocks later") {
        rig.advance(100);
        REQUIRE(rig.at(0) == approx(10.0f + 0.5f * static_cast<float>(100 * kBlockSize)));
    }
}

TEST_CASE("A reversed clip plays the region it was given, backwards", "[engine][clip][voice]") {
    // The model holds the anchor in the original file's coordinates, which is
    // what an editor goes on showing and what the project file keeps. What
    // reverse changes is the order the region comes out in, and the first
    // sample heard is the one at its far end.
    Rig rig;

    auto clip = clipOver(1, blocks(100, 200), 10000);
    clip.events[0].sourceDurationSeconds = 10.0;
    clip.events[0].reversed = true;
    rig.lane.audio.push_back(clip);

    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    rig.start(100, 40);

    const auto region = 100 * kBlockSize;

    REQUIRE(rig.at(0) == approx(static_cast<float>(10000 + region - 1)));
    REQUIRE(rig.at(1) == approx(static_cast<float>(10000 + region - 2)));
    REQUIRE(rig.at(kBlockSize - 1) == approx(static_cast<float>(10000 + region - kBlockSize)));

    SECTION("and ends on the sample it was trimmed to") {
        rig.advance(99);
        REQUIRE(rig.at(kBlockSize - 1) == approx(10000.0f));
    }

    // The reader is still reading forwards through a file that happens to be
    // the other way round, so nothing about this is a seek.
    CHECK(stream.underruns() == 0);
}

TEST_CASE("A reversed clip trimmed longer than its source arrives on time",
          "[engine][clip][voice]") {
    // The silence a mirrored read begins with. A clip drawn longer than the
    // audio behind it has that silence at its end going forwards, and at its
    // start going backwards, so the stream is cued in front of its own first
    // sample and has to walk through the gap rather than wait it out: a stream
    // that stood still would reach the material with its read-ahead somewhere
    // else, seek, and lose the block the material begins in.
    Rig rig;

    // Two blocks short of a tenth of a second of source, under a clip that
    // reads two hundred blocks, so the mirror starts 136 blocks before it.
    constexpr std::int64_t kSource = 4096;
    constexpr int kSilentBlocks = (200 * kBlockSize - kSource) / kBlockSize;

    auto clip = clipOver(1, blocks(100, 300));
    clip.events[0].sourceDurationSeconds = static_cast<double>(kSource) / kSampleRate;
    clip.events[0].reversed = true;
    rig.lane.audio.push_back(clip);

    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>(kSource));
    rig.publish();

    rig.start(100, 40);

    SECTION("the silence in front of the material is silence") {
        for (auto sample = 0; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.at(sample) == approx(0.0f));
        }
    }

    SECTION("and the material begins in the block it was placed in") {
        rig.advance(kSilentBlocks);

        REQUIRE(rig.at(0) == approx(static_cast<float>(kSource - 1)));
        REQUIRE(rig.at(1) == approx(static_cast<float>(kSource - 2)));

        // The point of walking through the silence rather than waiting it out.
        CHECK(stream.underruns() == 0);
    }
}

TEST_CASE("A reversed looped clip tiles the region it was given, backwards",
          "[engine][clip][voice]") {
    // Both derivations at once, and the reason they are one function: the
    // mirrored loop region comes from sourceReadFor and the phase the event
    // starts on comes from placementFor, and a reader pointed by one and read
    // by the other would play a tile from somewhere neither of them named.
    //
    // What plays first is what the forward clip would have played last, which
    // for a loop is wherever its phase had got to by the end.
    Rig rig;

    constexpr std::int64_t kLoopStart = 1000;
    constexpr std::int64_t kLoopLength = 100;
    constexpr int kRegion = 200 * kBlockSize;

    auto clip = clipOver(1, blocks(100, 300), kLoopStart);
    clip.events[0].sourceDurationSeconds = 10.0;
    clip.events[0].reversed = true;
    clip.events[0].loopEnabled = true;
    clip.events[0].loopStartSamples = kLoopStart;
    clip.events[0].loopLengthSamples = kLoopLength;

    // Where the forward clip ends inside the region, which is where the
    // backwards one begins.
    std::int64_t phase = (kRegion - 1) % kLoopLength;

    SECTION("anchored at the top of the region") {
        // phase stays where it is: the anchor is the loop start.
    }

    SECTION("anchored part way into it") {
        clip.events[0].anchorSamples = kLoopStart + 50;
        phase = (50 + kRegion - 1) % kLoopLength;
    }

    rig.lane.audio.push_back(clip);
    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    rig.start(100, 40);

    /// The sample of the file heard @p sample outputs after the clip begins:
    /// down from the phase it started on, round the region and down again.
    const auto expected = [phase](int sample) {
        const auto position = (phase - sample) % kLoopLength;
        return static_cast<float>(kLoopStart + (position < 0 ? position + kLoopLength : position));
    };

    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(expected(sample)));
    }

    // Still on that phase blocks later, over a wrap that is not a multiple of
    // the block: the tiling is the reader's and nothing here counts tiles.
    rig.advance(37);
    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(expected(37 * kBlockSize + sample)));
    }

    CHECK(stream.underruns() == 0);
}

TEST_CASE("A loop over a region longer than its file comes round again", "[engine][clip][voice]") {
    // The region is what the model asked for, so a region reaching past the end
    // of its own file plays that silence every time round. Its tail here is far
    // longer than a prefetch chunk, which is the shape that would stop a reader
    // for good if silence outside the file were reported as a reader that had
    // stopped answering: the top of the loop would never come round.
    Rig rig;

    constexpr std::int64_t kSource = 1000;
    constexpr std::int64_t kLoopStart = 900;
    constexpr std::int64_t kLoopLength = 32 * kBlockSize;

    auto clip = clipOver(1, blocks(100, 400), kLoopStart);
    clip.events[0].sourceDurationSeconds = static_cast<double>(kSource) / kSampleRate;
    clip.events[0].loopEnabled = true;
    clip.events[0].loopStartSamples = kLoopStart;
    clip.events[0].loopLengthSamples = kLoopLength;
    rig.lane.audio.push_back(clip);

    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>(kSource));
    rig.publish();

    rig.start(100, 40);

    // The hundred samples the file does have, from where the clip was trimmed.
    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(static_cast<float>(kLoopStart + sample)));
    }

    SECTION("the rest of the region is the silence the file has there") {
        rig.advance(4);
        for (auto sample = 0; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.at(sample) == approx(0.0f));
        }
    }

    SECTION("and the next tile plays the material again") {
        rig.advance(32);

        REQUIRE(rig.at(0) == approx(static_cast<float>(kLoopStart)));
        REQUIRE(rig.at(kBlockSize - 1) == approx(static_cast<float>(kLoopStart + kBlockSize - 1)));
        CHECK(stream.underruns() == 0);
    }
}

TEST_CASE("A looped clip tiles its region without a seek", "[engine][clip][voice]") {
    // A wrap is a discontinuity in the file and nowhere else: it happens inside
    // the reading, below the stream, so the reader is never pointed at the top
    // of the region and never pays for a block of silence on the way back to it
    // (PrefetchStream::seek).
    Rig rig;

    auto clip = clipOver(1, blocks(100, 300), 1000);
    clip.events[0].loopEnabled = true;
    clip.events[0].loopStartSamples = 1000;
    clip.events[0].loopLengthSamples = 100;
    rig.lane.audio.push_back(clip);

    auto& stream = rig.give(1, 1, std::make_unique<CountingReader>());
    rig.publish();

    rig.start(100, 40);

    for (auto sample = 0; sample < kBlockSize; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) == approx(static_cast<float>(1000 + sample % 100)));
    }

    SECTION("the block the wrap falls in carries straight on into the next tile") {
        rig.advance();
        for (auto sample = 0; sample < kBlockSize; ++sample) {
            INFO("sample " << sample);
            REQUIRE(rig.at(sample) ==
                    approx(static_cast<float>(1000 + (kBlockSize + sample) % 100)));
        }

        CHECK(stream.underruns() == 0);
    }

    SECTION("and it is still going round a hundred blocks later") {
        rig.advance(100);
        REQUIRE(rig.at(0) == approx(static_cast<float>(1000 + (100 * kBlockSize) % 100)));
        CHECK(stream.underruns() == 0);
    }
}

TEST_CASE("The launch ramp takes the step out of a voice that begins mid-material",
          "[engine][clip][voice]") {
    Rig rig;

    auto clip = clipOver(1, blocks(0, 1000));
    clip.launchFadeSamples = 32;
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    // A locate: one block to hand the cue over, and the next is the voice's
    // first, landing in material that is already at full swing.
    rig.start(500, 1);

    REQUIRE(rig.at(0) == approx(0.0f));
    REQUIRE(rig.at(31) == approx(1.0f));
    REQUIRE(rig.at(32) == approx(1.0f));

    // Monotonic on the way up: a correction that overshoots is a click of its
    // own rather than the absence of one.
    for (auto sample = 1; sample < 32; ++sample) {
        INFO("sample " << sample);
        REQUIRE(rig.at(sample) >= rig.at(sample - 1));
    }

    SECTION("and it is gone by the next block") {
        rig.advance();
        REQUIRE(rig.at(0) == approx(1.0f));
    }
}

TEST_CASE("A block the reader half filled is not a voice that carried on",
          "[engine][clip][voice]") {
    // A read that comes back short leaves the rest of the block silent, so the
    // block after it starts mid-material exactly as one after a total underrun
    // does. Counting any delivery at all as carrying on would let that one
    // resume out of silence with no ramp to take the step out of it.
    Rig rig;

    auto clip = clipOver(1, blocks(0, 1000));
    clip.launchFadeSamples = 32;
    rig.lane.audio.push_back(clip);

    // A pool of 288 samples, which four blocks of 64 do not divide: the fifth
    // gets the 32 samples that are left and nothing more.
    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f), {96, 3});
    rig.publish();

    rig.fill();
    rig.autoFill = false;

    rig.advance(4);
    REQUIRE(rig.at(kBlockSize - 1) == approx(1.0f));

    rig.advance();
    REQUIRE(rig.at(31) == approx(1.0f));
    REQUIRE(rig.at(32) == approx(0.0f));

    // The reader catches up, and the block that resumes is ramped.
    rig.autoFill = true;
    rig.advance();

    REQUIRE(rig.at(0) == approx(0.0f));
    REQUIRE(rig.at(31) == approx(1.0f));
}

TEST_CASE("A launch ramp of zero preserves the leading transient exactly",
          "[engine][clip][voice]") {
    Rig rig;

    auto clip = clipOver(1, blocks(0, 1000));
    clip.launchFadeSamples = 0;
    rig.lane.audio.push_back(clip);

    rig.give(1, 1, std::make_unique<ConstantReader>(1.0f));
    rig.publish();

    rig.start(500, 1);
    REQUIRE(rig.at(0) == approx(1.0f));
}

TEST_CASE("A clip with no reader standing by is counted, not quietly dropped",
          "[engine][clip][voice]") {
    Rig rig;
    rig.lane.audio.push_back(clipOver(1, blocks(0, 400)));
    rig.publish();  // no stream given

    REQUIRE(rig.source.starvedVoices() == 0);

    rig.start(100, 0);

    for (auto sample = 0; sample < kBlockSize; ++sample)
        REQUIRE(rig.at(sample) == approx(0.0f));
    CHECK(rig.source.starvedVoices() == 1);

    rig.advance();
    CHECK(rig.source.starvedVoices() == 2);
}

TEST_CASE("A track wanting more simultaneous clips than it has voices says so",
          "[engine][clip][voice]") {
    Rig rig;

    constexpr auto kTooMany = magda::engine::kMaxVoicesPerTrack + 1;
    for (auto index = 0; index < kTooMany; ++index) {
        const auto id = index + 1;
        rig.lane.audio.push_back(clipOver(id, blocks(0, 1000)));
        rig.give(id, id, std::make_unique<ConstantReader>(1.0f));
    }

    rig.publish();
    rig.start(300, 100);

    // Every voice there is, and one clip that says it did not get one.
    REQUIRE(rig.at(0) == approx(static_cast<float>(magda::engine::kMaxVoicesPerTrack)));
    CHECK(rig.source.starvedVoices() > 0);
}

TEST_CASE("A voice is let go when its clip stops, and its slot is reusable",
          "[engine][clip][voice]") {
    // With every voice spoken for, a clip that ends has to release its slot in
    // the block it ends in, or the clip starting next block finds none free.
    Rig rig;

    constexpr auto kVoices = magda::engine::kMaxVoicesPerTrack;
    for (auto index = 0; index < kVoices; ++index) {
        const auto id = index + 1;
        rig.lane.audio.push_back(clipOver(id, blocks(0, 500)));
        rig.give(id, id, std::make_unique<ConstantReader>(1.0f));
    }

    // One more that starts where the others stop.
    rig.lane.audio.push_back(clipOver(kVoices + 1, blocks(500, 1000)));
    rig.give(kVoices + 1, kVoices + 1, std::make_unique<ConstantReader>(1.0f));

    rig.publish();

    rig.start(300, 100);
    REQUIRE(rig.at(0) == approx(static_cast<float>(kVoices)));

    const auto before = rig.source.starvedVoices();
    rig.advance(300);  // block 600, past the handover

    REQUIRE(rig.at(0) == approx(1.0f));
    CHECK(rig.source.starvedVoices() == before);
}

TEST_CASE("A track with nothing published renders silence rather than the buffer's contents",
          "[engine][clip][voice]") {
    Rig rig;

    juce::AudioBuffer<float> dirty(2, kBlockSize);
    for (auto channel = 0; channel < 2; ++channel)
        for (auto sample = 0; sample < kBlockSize; ++sample)
            dirty.setSample(channel, sample, 0.5f);

    rig.source.render(blockFrom(blockTime(100)), juce::dsp::AudioBlock<float>(dirty));

    for (auto sample = 0; sample < kBlockSize; ++sample)
        REQUIRE(dirty.getSample(0, sample) == approx(0.0f));
}

TEST_CASE("The fade curves are the shapes the incumbent draws", "[engine][clip][fades]") {
    using magda::FadeCurve;
    using magda::engine::fadeGain;

    const auto every = {FadeCurve::Linear, FadeCurve::Convex, FadeCurve::Concave,
                        FadeCurve::SCurve};

    SECTION("every curve runs from silence to unity") {
        for (const auto curve : every) {
            INFO("curve " << static_cast<int>(curve));
            REQUIRE(fadeGain(curve, 0.0f) == approx(0.0f));
            REQUIRE(fadeGain(curve, 1.0f) == approx(1.0f));
        }
    }

    SECTION("and each one is the shape it is named after") {
        REQUIRE(fadeGain(FadeCurve::Linear, 0.5f) == approx(0.5f));
        REQUIRE(fadeGain(FadeCurve::Convex, 0.5f) > 0.5f);
        REQUIRE(fadeGain(FadeCurve::Concave, 0.5f) < 0.5f);
        REQUIRE(fadeGain(FadeCurve::SCurve, 0.5f) == approx(0.5f));
    }

    SECTION("monotonic, so a fade never dips on its way") {
        for (const auto curve : every) {
            auto previous = 0.0f;
            for (auto step = 0; step <= 100; ++step) {
                const auto gain = fadeGain(curve, static_cast<float>(step) / 100.0f);
                INFO("curve " << static_cast<int>(curve) << " step " << step);
                REQUIRE(gain >= previous - 1e-6f);
                previous = gain;
            }
        }
    }
}
