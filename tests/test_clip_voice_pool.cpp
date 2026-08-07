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
using magda::engine::kMaxReadersPerTrack;
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

    // All over the same stretch, so every one of them is asking to be heard at
    // the same instant.
    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kMaxVoicesPerTrack + 3; ++index)
        lane.push_back(clipAt(index + 1, 0.0, 4.0));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(1.0);
    pool.service();

    // A reader each, because the budget is not the ceiling, and three
    // reported, because that is how many of them will not be heard.
    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxVoicesPerTrack + 3));
    CHECK(pool.overSubscribed() == 3);

    SECTION("and stops reporting once the lane thins out") {
        pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0)}));
        pool.service();

        CHECK(pool.streamCount() == 1);
        CHECK(pool.overSubscribed() == 0);
    }
}

TEST_CASE("Clips that follow one another are not clips stacked on one another",
          "[engine][clip][pool]") {
    // Chopped material: a dozen slices inside the window, never two of them
    // wanted by one callback. Counting window membership rather than
    // concurrency would report a track in trouble that is playing exactly one
    // clip at a time.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    constexpr auto kSlices = kMaxVoicesPerTrack - 4;

    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kSlices; ++index)
        lane.push_back(clipAt(index + 1, index * 0.05, (index + 1) * 0.05));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(0.0);
    pool.service();

    CHECK(pool.overSubscribed() == 0);

    // And every one of them has its reader well before it is due, because a
    // slice needs a reader even when it never needs a second voice.
    CHECK(pool.streamCount() == static_cast<std::size_t>(kSlices));

    // Which the callback agrees with: it renders one slice and starves nothing.
    ClipSnapshotFeed clips;
    ClipAudioSource source(kTrack, clips, pool.feed());
    source.prepare(context());

    auto live = std::make_shared<ClipSnapshot>();
    TrackClipPlayback track;
    track.trackId = kTrack;
    for (auto index = 0; index < kSlices; ++index)
        track.audio.push_back(clipAt(index + 1, index * 0.05, (index + 1) * 0.05));
    live->tracks.push_back(std::move(track));
    clips.publish(std::move(live));

    juce::AudioBuffer<float> output(2, kBlockSize);
    source.render(blockFrom(0.1), juce::dsp::AudioBlock<float>(output));
    CHECK(source.starvedVoices() == 0);
}

TEST_CASE("Slices too short to fill a callback are concurrent, and say so",
          "[engine][clip][pool]") {
    // Clips shorter than a block share one, so a callback needs a voice for
    // each of them however little they overlap. Measuring bare overlap would
    // report this track in the clear while the callback was dropping every
    // slice past the sixteenth.
    //
    // Three samples each, back to back: a block spans twenty-two of them, and
    // the whole run fits inside the reader budget, so the only thing this can
    // be short of is voices.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    constexpr auto kSliceSeconds = 3.0 / kSampleRate;
    constexpr auto kSlices = kMaxReadersPerTrack;

    std::vector<AudioClipPlayback> lane;
    TrackClipPlayback track;
    track.trackId = kTrack;
    for (auto index = 0; index < kSlices; ++index) {
        lane.push_back(clipAt(index + 1, index * kSliceSeconds, (index + 1) * kSliceSeconds));
        track.audio.push_back(lane.back());
    }

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(0.0);
    pool.service();

    // Every one of them has a reader, so nothing here is waiting on a disk.
    REQUIRE(pool.streamCount() == static_cast<std::size_t>(kSlices));
    CHECK(pool.unbridged() == 0);
    CHECK(pool.overSubscribed() > 0);

    // And the callback is where it costs something.
    ClipSnapshotFeed clips;
    ClipAudioSource source(kTrack, clips, pool.feed());
    source.prepare(context());

    auto live = std::make_shared<ClipSnapshot>();
    live->tracks.push_back(std::move(track));
    clips.publish(std::move(live));

    juce::AudioBuffer<float> output(2, kBlockSize);
    source.render(blockFrom(kSliceSeconds), juce::dsp::AudioBlock<float>(output));
    CHECK(source.starvedVoices() > 0);
}

TEST_CASE("A lane the budget cannot reach ahead of says so, and is not called stacked",
          "[engine][clip][pool]") {
    // The failure the voice ceiling has nothing to do with. Eight-sample
    // slices: a callback wants nine of them at once, well inside the ceiling,
    // but the whole reader budget covers a quarter of a millisecond of them and
    // the pool will not look again for ten. Every clip past the budget fits
    // and still plays nothing.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    constexpr auto kSliceSeconds = 8.0 / kSampleRate;

    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kMaxReadersPerTrack * 4; ++index)
        lane.push_back(clipAt(index + 1, index * kSliceSeconds, (index + 1) * kSliceSeconds));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(0.0);
    pool.service();

    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxReadersPerTrack));

    // Not too many at once. Too many too soon, which is its own count.
    CHECK(pool.overSubscribed() == 0);
    CHECK(pool.unbridged() > 0);
}

TEST_CASE("A clip with no voice free is not also blamed on the reader budget",
          "[engine][clip][pool]") {
    // Enough clips over one stretch to exhaust the readers and the voices
    // together. The ones the budget turned away could not have sounded if every
    // one of them had had a reader, so they belong to the ceiling and to
    // nothing else; counting them twice would point at the wrong limit.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kMaxReadersPerTrack + 8; ++index)
        lane.push_back(clipAt(index + 1, 0.0, 4.0));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(1.0);
    pool.service();

    REQUIRE(pool.streamCount() == static_cast<std::size_t>(kMaxReadersPerTrack));
    CHECK(pool.overSubscribed() == kMaxReadersPerTrack + 8 - kMaxVoicesPerTrack);
    CHECK(pool.unbridged() == 0);
}

TEST_CASE("Clips the budget dropped take up the voices they were counted for",
          "[engine][clip][pool]") {
    // The readers are all spent on clips that are over before the bridge, so
    // nothing the pool kept is playing when the next lot arrive. Twenty of them
    // start together: sixteen the budget failed, and four the ceiling would
    // have refused whatever the budget had been. Without the counted clips
    // taking up voices against each other, all twenty look like budget
    // failures and four of them are counted twice.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    std::vector<AudioClipPlayback> lane;

    // Sequential, short, and finished well before the crowd arrives.
    for (auto index = 0; index < kMaxReadersPerTrack; ++index)
        lane.push_back(clipAt(index + 1, index * 0.001, (index + 1) * 0.001));

    constexpr auto kCrowd = kMaxVoicesPerTrack + 4;
    for (auto index = 0; index < kCrowd; ++index)
        lane.push_back(clipAt(kMaxReadersPerTrack + index + 1, 0.05, 0.5));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(0.0);
    pool.service();

    REQUIRE(pool.streamCount() == static_cast<std::size_t>(kMaxReadersPerTrack));

    CHECK(pool.overSubscribed() == kCrowd - kMaxVoicesPerTrack);
    CHECK(pool.unbridged() == kMaxVoicesPerTrack);

    // Which is the point: every clip that will not sound is named once.
    CHECK(pool.overSubscribed() + pool.unbridged() == kCrowd);
}

TEST_CASE("A clip that fits the ceiling is provisioned before the next round",
          "[engine][clip][pool]") {
    // What the reader budget is for. Sixteen clips sounding and more starting
    // in the moments after: sized to the voice ceiling the budget would go
    // entirely on what is playing, and every clip after it would be opened at
    // the instant it was due, which is a seek.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kMaxVoicesPerTrack; ++index)
        lane.push_back(clipAt(index + 1, 0.0, 4.0));

    // And one starting inside the bridge, which is where a reader has to exist
    // already for it to be ready in time.
    const auto soon = magda::engine::kReadAheadBridgeSeconds / 2.0;
    lane.push_back(clipAt(kMaxVoicesPerTrack + 1, soon, 4.0));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(0.0);
    pool.service();

    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxVoicesPerTrack + 1));
    CHECK(pool.unbridged() == 0);
}

TEST_CASE("A window holding more clips than there are readers takes the soonest",
          "[engine][clip][pool]") {
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    std::vector<AudioClipPlayback> lane;
    for (auto index = 0; index < kMaxReadersPerTrack + 5; ++index)
        lane.push_back(clipAt(index + 1, index * 0.02, (index + 1) * 0.02));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(0.0);
    pool.service();

    // The budget is spent, and none of it is reported: nothing here will fail
    // to sound, because each slice is passed and gives its reader back long
    // before the ones behind it are due.
    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxReadersPerTrack));
    CHECK(pool.overSubscribed() == 0);
    CHECK(pool.unbridged() == 0);

    // Rolling past the first few hands their readers to the ones behind.
    pool.setPosition(0.1);
    pool.service();

    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxReadersPerTrack));
}

TEST_CASE("A round that changes nothing does not swap a table", "[engine][clip][pool]") {
    // An idle session services a hundred times a second, and every publish
    // waits for the block the callback is in.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0)}));
    pool.setPosition(0.0);
    pool.service();

    REQUIRE(pool.tablesPublished() == 1);

    pool.service();
    pool.service();
    pool.service();

    CHECK(pool.tablesPublished() == 1);

    SECTION("and swaps one again as soon as something does change") {
        pool.setPosition(10.0);
        pool.service();

        CHECK(pool.tablesPublished() == 2);
    }
}

TEST_CASE("A reader is reopened when the file behind its clip changes", "[engine][clip][pool]") {
    // Ids outlive the edits that change what they name. A reader kept on the
    // strength of its id alone would go on playing a file the clip no longer
    // points at, for as long as the clip stayed in the window.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0, 0, "before.wav")}));
    pool.setPosition(0.0);
    pool.service();

    REQUIRE(files.opens == 1);
    REQUIRE(pool.streamCount() == 1);

    // Same track, same clip, same event, different material.
    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0, 0, "after.wav")}));
    pool.service();

    CHECK(files.opens == 2);
    CHECK(pool.streamCount() == 1);
    CHECK(reader.streamCount() == 1);

    SECTION("and left alone when nothing about it moved") {
        pool.service();
        CHECK(files.opens == 2);
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
    // just ahead of it to use up every reader twice over.
    lane.push_back(clipAt(1, 0.0, 10.0));
    for (auto index = 0; index < kMaxReadersPerTrack * 2; ++index)
        lane.push_back(clipAt(index + 2, 5.2 + index * 0.01, 9.0));

    pool.setSnapshot(snapshotOf(std::move(lane)));
    pool.setPosition(5.0);
    pool.service();

    CHECK(pool.streamCount() == static_cast<std::size_t>(kMaxReadersPerTrack));

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

TEST_CASE("A reversed clip is cued where a reversed clip starts", "[engine][clip][pool]") {
    // The same end to end shape over a mirrored reading. The point of it is
    // that both sides work the mirrored anchor out through the same function
    // (clip/EventPlacement.hpp): a reader pointed by one derivation and read by
    // another would play from somewhere neither of them named, and it would
    // sound like material rather than like a fault.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    constexpr std::int64_t kAnchor = 1000;

    // A ten second file, and a second and a half of it from the thousandth
    // sample. What plays first is the sample at the far end of that.
    auto clip = clipAt(1, 0.5, 2.0, kAnchor);
    clip.events[0].sourceDurationSeconds = 10.0;
    clip.events[0].reversed = true;

    auto snapshot = snapshotOf({clip});

    pool.setSnapshot(snapshot);
    pool.setPosition(0.0);
    pool.service();
    REQUIRE(pool.streamCount() == 1);

    ClipSnapshotFeed clips;
    ClipAudioSource source(kTrack, clips, pool.feed());
    source.prepare(context());
    clips.publish(snapshot);

    juce::AudioBuffer<float> output(2, kBlockSize);

    source.render(blockFrom(0.0, false), juce::dsp::AudioBlock<float>(output));
    while (reader.fillOnce()) {
    }

    source.render(blockFrom(0.5), juce::dsp::AudioBlock<float>(output));

    const auto last = kAnchor + static_cast<std::int64_t>(std::llround(1.5 * kSampleRate)) - 1;

    CHECK(output.getSample(0, 0) == Catch::Approx(static_cast<float>(last)).margin(1e-4));
    CHECK(output.getSample(0, kBlockSize - 1) ==
          Catch::Approx(static_cast<float>(last - kBlockSize + 1)).margin(1e-4));
    CHECK(source.starvedVoices() == 0);
}

TEST_CASE("A reader is reopened when what its clip asks of the file changes",
          "[engine][clip][pool]") {
    // Reverse, looping and rate conversion are built into the reader rather
    // than asked of it block by block (io/SourceReaders.hpp), so a clip that
    // has been reversed since is reading a different file from the same path
    // and the reader it had cannot answer for it.
    TestFiles files;
    PrefetchThread reader;
    ClipVoicePool pool(files, reader, context());

    pool.setSnapshot(snapshotOf({clipAt(1, 0.0, 4.0)}));
    pool.setPosition(0.0);
    pool.service();

    REQUIRE(files.opens == 1);

    auto reversed = clipAt(1, 0.0, 4.0);
    reversed.events[0].reversed = true;
    pool.setSnapshot(snapshotOf({reversed}));
    pool.service();

    CHECK(files.opens == 2);
    CHECK(pool.streamCount() == 1);
    CHECK(reader.streamCount() == 1);

    SECTION("and left alone once it is the reading it was opened for") {
        pool.service();
        CHECK(files.opens == 2);
    }
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
