#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "clip/ClipAudioSource.hpp"
#include "clip/ClipVoicePool.hpp"

/**
 * Which clips have a reader standing by, and where it is pointed (#2035).
 *
 * The other half of the voice layer: test_clip_voice.cpp asks what a voice does
 * with a stream, this asks where the stream came from and when it goes away.
 */

using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::AudioFileReader;
using magda::engine::BlockInfo;
using magda::engine::ClipAudioSource;
using magda::engine::ClipSnapshot;
using magda::engine::ClipSnapshotFeed;
using magda::engine::ClipVoicePool;
using magda::engine::kCueAheadSeconds;
using magda::engine::kMaxVoicesPerTrack;
using magda::engine::PrefetchThread;
using magda::engine::RenderContext;
using magda::engine::SnapshotSpan;
using magda::engine::TrackClipPlayback;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
constexpr magda::TrackId kTrack = 3;

class CountingReader final : public AudioFileReader {
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
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t startSample,
             int numSamples) override {
        for (auto channel = 0; channel < destination.getNumChannels(); ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                destination.setSample(channel, destinationOffset + sample,
                                      static_cast<float>(startSample + sample));
        return numSamples;
    }
};

/// Opens everything but the one path that stands for a file that has moved.
class TestFiles final : public magda::engine::AudioFileReaderFactory {
  public:
    static constexpr const char* kMissing = "gone.wav";

    std::unique_ptr<AudioFileReader> open(const std::string& path) override {
        ++opens;
        if (path == kMissing)
            return nullptr;
        return std::make_unique<CountingReader>();
    }

    int opens = 0;
};

RenderContext context() {
    return RenderContext{kSampleRate, kBlockSize, 2};
}

SnapshotSpan seconds(double start, double end) {
    SnapshotSpan span;
    span.startBeat = start * 2.0;
    span.endBeat = end * 2.0;
    span.startSeconds = start;
    span.endSeconds = end;
    return span;
}

AudioClipPlayback clipAt(magda::ClipId id, double start, double end, std::int64_t anchor = 0,
                         const std::string& path = "take.wav") {
    AudioClipPlayback clip;
    clip.clipId = id;
    clip.span = seconds(start, end);
    clip.launchFadeSamples = 0;

    AudioEventPlayback event;
    event.eventId = id;
    event.sourceId = id;
    event.filePath = path;
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 1000.0;
    event.span = seconds(start, end);
    event.anchorSamples = anchor;
    clip.events.push_back(std::move(event));

    return clip;
}

std::shared_ptr<const ClipSnapshot> snapshotOf(std::vector<AudioClipPlayback> clips) {
    auto snapshot = std::make_shared<ClipSnapshot>();
    TrackClipPlayback track;
    track.trackId = kTrack;
    track.audio = std::move(clips);
    snapshot->tracks.push_back(std::move(track));
    return snapshot;
}

BlockInfo blockFrom(double startSeconds, bool continuous = true) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;
    block.startSeconds = startSeconds;
    block.endSeconds = startSeconds + kBlockSize / kSampleRate;
    block.startBeat = startSeconds * 2.0;
    block.endBeat = block.endSeconds * 2.0;
    block.continuous = continuous;
    return block;
}

}  // namespace

TEST_CASE("Only the clips the transport is about to reach get a reader", "[engine][clip][pool]") {
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 1.0), clipAt(2, 3.0, 4.0), clipAt(3, 30.0, 31.0)}));

    SECTION("at the top, the clip that is playing and nothing beyond the window") {
        pool.setPosition(0.0);
        pool.service();

        CHECK(pool.streamCount() == 1);
        CHECK(files.opens == 1);
        CHECK(pool.overSubscribed() == 0);
    }

    SECTION("a clip inside the window is provisioned before it sounds") {
        // A second ahead of a clip that has not started: the whole point.
        pool.setPosition(3.0 - kCueAheadSeconds / 2.0);
        pool.service();

        CHECK(pool.streamCount() == 1);
        CHECK(files.opens == 1);
    }

    SECTION("and a clip beyond it is left alone") {
        pool.setPosition(3.0 - kCueAheadSeconds * 2.0);
        pool.service();

        CHECK(pool.streamCount() == 0);
        CHECK(files.opens == 0);
    }

    SECTION("a round that changes nothing opens nothing") {
        pool.setPosition(0.0);
        pool.service();
        pool.service();
        pool.service();

        CHECK(pool.streamCount() == 1);
        CHECK(files.opens == 1);
    }
}

TEST_CASE("A clip the transport has passed gives its reader back", "[engine][clip][pool]") {
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 1.0), clipAt(2, 3.0, 4.0)}));

    pool.setPosition(0.0);
    pool.service();
    REQUIRE(pool.streamCount() == 1);
    REQUIRE(reader.streamCount() == 1);

    pool.setPosition(2.5);
    pool.service();

    // The one behind is closed, the one ahead is open, and the reader thread
    // knows about exactly the streams that still exist.
    CHECK(pool.streamCount() == 1);
    CHECK(reader.streamCount() == 1);
    CHECK(files.opens == 2);

    SECTION("and gets a fresh one when the transport comes back to it") {
        pool.setPosition(0.0);
        pool.service();

        CHECK(pool.streamCount() == 1);
        CHECK(files.opens == 3);
    }
}

TEST_CASE("A file that will not open is reported once, not retried every round",
          "[engine][clip][pool]") {
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0, 0, TestFiles::kMissing)}));
    pool.setPosition(0.0);

    pool.service();
    CHECK(pool.unreadableFiles() == 1);
    CHECK(files.opens == 1);
    CHECK(reader.streamCount() == 0);

    pool.service();
    pool.service();

    // Still reported, still not reopened: a path that failed stays failed for
    // as long as it sits in the window.
    CHECK(pool.unreadableFiles() == 1);
    CHECK(files.opens == 1);
}

TEST_CASE("A lane stacking more clips than a track has voices reports the excess",
          "[engine][clip][pool]") {
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kMaxVoicesPerTrack + 3; ++index)
        lane.push_back(clipAt(index + 1, 0.0, 4.0));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(1.0);
    pool.service();

    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxVoicesPerTrack));
    CHECK(pool.overSubscribed() == 3);

    SECTION("and stops reporting once the lane thins out") {
        pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0)}));
        pool.service();

        CHECK(pool.streamCount() == 1);
        CHECK(pool.overSubscribed() == 0);
    }
}

TEST_CASE("A clip that is sounding keeps its reader over one that has not started",
          "[engine][clip][pool]") {
    // Which clips a crowded lane drops has to be decided rather than left to
    // the order a round walked it: taking a stream off a clip mid-note is worse
    // than not having pointed one at the next clip yet.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    std::vector<AudioClipPlayback> lane;

    // One long clip the transport is standing inside, and enough clips starting
    // just ahead of it to use up every remaining voice twice over.
    lane.push_back(clipAt(1, 0.0, 10.0));
    for (auto index = 0; index < kMaxVoicesPerTrack * 2; ++index)
        lane.push_back(clipAt(index + 2, 5.2 + index * 0.01, 9.0));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(5.0);
    pool.service();

    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxVoicesPerTrack));
    CHECK(pool.overSubscribed() == kMaxVoicesPerTrack + 1);

    // The clip that is playing is one of the ones that kept a reader: render a
    // block of it and it sounds.
    ClipSnapshotFeed clips;
    ClipAudioSource source(kTrack, clips, pool.feed());
    source.prepare(context());

    auto live = std::make_shared<ClipSnapshot>();
    live->tracks.push_back(TrackClipPlayback{kTrack, {clipAt(1, 0.0, 10.0)}, {}});
    clips.publish(std::move(live));

    juce::AudioBuffer<float> output(2, kBlockSize);
    source.render(blockFrom(5.0), juce::dsp::AudioBlock<float>(output));
    CHECK(source.starvedVoices() == 0);
}

TEST_CASE("A clip provisioned ahead of the transport plays from its first sample",
          "[engine][clip][pool]") {
    // The end to end shape of the slice: the pool opens and cues the file a
    // second before the clip is due, the source hands the cue on at the top of
    // a block, the reader fills, and the block the clip starts in plays
    // material rather than the silence a seek would have cost (#2016).
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    constexpr std::int64_t kAnchor = 1000;
    auto snapshot = snapshotOf({clipAt(1, 0.5, 2.0, kAnchor)});

    pool.setSnapshot(snapshot);
    pool.setPosition(0.0);
    pool.service();
    REQUIRE(pool.streamCount() == 1);

    ClipSnapshotFeed clips;
    ClipAudioSource source(kTrack, clips, pool.feed());
    source.prepare(context());
    clips.publish(snapshot);

    juce::AudioBuffer<float> output(2, kBlockSize);

    // A block before the clip starts, which is what carries the cue across to
    // the callback's side, and then as much read-ahead as the pool will hold.
    source.render(blockFrom(0.0, false), juce::dsp::AudioBlock<float>(output));
    while (reader.fillOnce()) {
    }

    source.render(blockFrom(0.5), juce::dsp::AudioBlock<float>(output));

    CHECK(output.getSample(0, 0) == Catch::Approx(static_cast<float>(kAnchor)).margin(1e-4));
    CHECK(output.getSample(0, kBlockSize - 1) ==
          Catch::Approx(static_cast<float>(kAnchor + kBlockSize - 1)).margin(1e-4));
    CHECK(source.starvedVoices() == 0);
}

TEST_CASE("A pool with nothing published provisions nothing", "[engine][clip][pool]") {
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.service();

    CHECK(pool.streamCount() == 0);
    CHECK(files.opens == 0);
    CHECK(pool.overSubscribed() == 0);

    // And a source reading its feed renders silence rather than refusing to run.
    ClipSnapshotFeed clips;
    ClipAudioSource source(kTrack, clips, pool.feed());
    source.prepare(context());

    juce::AudioBuffer<float> output(2, kBlockSize);
    for (auto channel = 0; channel < 2; ++channel)
        for (auto sample = 0; sample < kBlockSize; ++sample)
            output.setSample(channel, sample, 0.25f);

    source.render(blockFrom(1.0), juce::dsp::AudioBlock<float>(output));

    for (auto sample = 0; sample < kBlockSize; ++sample)
        REQUIRE(output.getSample(0, sample) == Catch::Approx(0.0f).margin(1e-4));
}
