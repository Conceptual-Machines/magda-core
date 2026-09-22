#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "ClipCallback.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStreamFeed.hpp"
#include "clip/ClipStretcher.hpp"
#include "clip/ClipVoice.hpp"
#include "clip/ClipVoicePool.hpp"
#include "clip/EventPlacement.hpp"
#include "core/TimeStretchModes.hpp"
#include "io/PrefetchThread.hpp"
#include "io/SourceReaders.hpp"
#include "launch/LaunchRequests.hpp"
#include "launch/SessionLauncher.hpp"
#include "transport/TempoMap.hpp"

// A stretcher primed off the audio thread for a start the voice pool saw coming, which the voice
// takes instead of priming in its callback (#2786). Taking it has to be inaudible: a start that
// took one renders exactly what priming it on the audio thread renders.

using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::BlockInfo;
using magda::engine::ClipStretcher;
using magda::engine::ClipVoice;
using magda::engine::PrefetchStream;
using magda::engine::RenderContext;
using magda::engine::StandbyStretcher;
using magda::engine::TempoMap;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 256;
constexpr double kEventStart = 1.0;
constexpr std::uint64_t kSnapshot = 5;

RenderContext context() {
    return RenderContext{kSampleRate, kBlockSize, 2};
}

class SineReader final : public magda::engine::AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 100000000;
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
            for (auto sample = 0; sample < numSamples; ++sample) {
                const auto at = static_cast<double>(startSample + sample);
                destination.setSample(
                    channel, destinationOffset + sample,
                    static_cast<float>(0.4 *
                                       std::sin(2.0 * juce::MathConstants<double>::pi *
                                                (330.0 + 110.0 * channel) * at / kSampleRate)));
            }
        return numSamples;
    }
};

/// One stretched clip, a stream over it, the entry's own stretcher and a voice, rolled a block
/// at a time the way the callback rolls it.
struct Rig {
    Rig() {
        clip.clipId = 1;
        clip.span.seconds = {kEventStart, kEventStart + 60.0};
        clip.span.beats = {tempo.timeToBeat(kEventStart), tempo.timeToBeat(kEventStart + 60.0)};
        clip.launchFadeSamples = 0;

        AudioEventPlayback event;
        event.eventId = 1;
        event.sourceId = 1;
        event.filePath = "take.wav";
        event.sourceSampleRate = kSampleRate;
        event.sourceDurationSeconds = 10000.0;
        event.span = clip.span;
        event.speedRatio = 1.5;
        event.timeStretchMode = magda::time_stretch_mode::kSignalsmith;
        clip.events.push_back(event);

        setup = magda::engine::stretchSetupFor(clip, clip.events.front(), context());
        active = magda::engine::makeStretcher(setup);
        REQUIRE(active != nullptr);
        preRoll = active->preRollSamples(setup.peakRate);

        stream = std::make_unique<PrefetchStream>(source(), context(),
                                                  magda::engine::PrefetchSettings{8192, 8});
        const auto opening = keyAt(kEventStart);
        stream->startAt(opening.readFrom - opening.preRoll, 0);

        voice.prepare(context());
        scratch.setSize(2, magda::engine::stretchScratchSamples(kBlockSize));
        out.setSize(2, kBlockSize);
    }

    std::unique_ptr<magda::engine::AudioFileReader> source() const {
        return magda::engine::readThrough(
            std::make_unique<SineReader>(),
            magda::engine::sourceReadFor(clip.events.front(), kSampleRate));
    }

    magda::engine::StretchPrimeKey keyAt(double seconds, std::uint64_t snapshot = kSnapshot) const {
        const auto& event = clip.events.front();
        return magda::engine::standbyKeyFor(
            *active, preRoll, clip, event,
            magda::engine::stretchCellOpening(event, seconds, kSampleRate), tempo, snapshot,
            kSampleRate);
    }

    /// Primed the way the pool primes one, off this thread's render path.
    std::shared_ptr<StandbyStretcher> standbyAt(double seconds,
                                                std::uint64_t snapshot = kSnapshot) const {
        auto reader = source();
        return magda::engine::primeStandby(keyAt(seconds, snapshot), setup, *reader, context());
    }

    BlockInfo blockAt(double seconds, bool continuous) const {
        BlockInfo block;
        block.numSamples = kBlockSize;
        block.playing = true;
        block.continuous = continuous;
        block.seconds = {seconds, seconds + kBlockSize / kSampleRate};
        block.beats = {tempo.timeToBeat(block.seconds.start), tempo.timeToBeat(block.seconds.end)};
        block.tempo = &tempo;
        return block;
    }

    /// Render the block at @p seconds, handing the voice @p stretcher and @p standby, and keep
    /// what it made.
    void render(double seconds, bool continuous, ClipStretcher* stretcher,
                StandbyStretcher* standby) {
        while (stream->fill()) {
        }
        out.clear();
        voice.render(clip, clip.events.front(), blockAt(seconds, continuous), *stream, stretcher,
                     preRoll, juce::dsp::AudioBlock<float>(scratch),
                     juce::dsp::AudioBlock<float>(out), false, standby, kSnapshot);
        for (auto channel = 0; channel < 2; ++channel)
            rendered.insert(rendered.end(), out.getReadPointer(channel),
                            out.getReadPointer(channel) + kBlockSize);
    }

    /// Hold the material a return to @p seconds reads, from its priming window on, the way the
    /// pool retains a loop's destination, so the start finds it resident either way.
    void recue(double seconds) {
        const auto opening = keyAt(seconds);
        auto retained = std::make_shared<PrefetchStream::RetainedRegion>();
        retained->startSample = opening.readFrom - opening.preRoll;
        const auto count = opening.preRoll + 16 * kBlockSize * 2;
        retained->audio.setSize(2, count);
        retained->audio.clear();
        retained->count = source()->read(retained->audio, 0, retained->startSample, count);
        REQUIRE(stream->retain(std::move(retained)));
    }

    /// Roll @p count continuous blocks from the block after @p fromBlock.
    void roll(int fromBlock, int count, ClipStretcher* stretcher,
              StandbyStretcher* standby = nullptr) {
        for (auto block = fromBlock + 1; block <= fromBlock + count; ++block)
            render(timeOf(block), true, stretcher, standby);
    }

    static double timeOf(int block) {
        return kEventStart + block * static_cast<double>(kBlockSize) / kSampleRate;
    }

    TempoMap tempo;
    AudioClipPlayback clip;
    magda::engine::StretchSetup setup;
    std::shared_ptr<ClipStretcher> active;
    int preRoll = 0;
    std::unique_ptr<PrefetchStream> stream;
    ClipVoice voice;
    juce::AudioBuffer<float> scratch;
    juce::AudioBuffer<float> out;
    std::vector<float> rendered;
};

class SineFiles final : public magda::engine::AudioFileReaderFactory {
  public:
    std::unique_ptr<magda::engine::AudioFileReader> open(const std::string&) override {
        return std::make_unique<SineReader>();
    }
};

struct LoopedRun {
    std::vector<float> rendered;
    int taken = 0;
    int wraps = 0;
};

/// How a looped run is set up. Lengths are in samples, so a loop is whole blocks at any size.
struct LoopedPlay {
    bool standbys = true;
    int blockSize = kBlockSize;
    int clipSamples = 60 * static_cast<int>(kSampleRate);
    int loopOffsetSamples = 40 * kBlockSize;
    int loopSamples = 60 * kBlockSize;
    int loops = 5;
    /// The block at which the clip's pitch is edited, or -1.
    int pitchEditAt = -1;
    bool loop = true;
    /// Where a stopped transport stands before it plays, or -1 to play from the loop's start.
    int cursorSamples = -1;
};

/// A stretched clip under a transport looping part of the timeline, through the real voice pool,
/// serviced and filled between blocks the way its thread and the prefetch thread do.
LoopedRun playLooped(const LoopedPlay& play) {
    constexpr magda::TrackId kTrack = 3;
    const RenderContext rate{kSampleRate, play.blockSize, 2};
    const TempoMap tempo;

    const auto clipAt = [&](float pitch) {
        Rig shape;
        auto clip = shape.clip;
        clip.span.seconds.end = kEventStart + play.clipSamples / kSampleRate;
        clip.span.beats.end = tempo.timeToBeat(clip.span.seconds.end);
        clip.events.front().span = clip.span;
        clip.events.front().pitchChange = pitch;
        return clip;
    };
    const auto snapshotOf = [&](float pitch, std::uint64_t serial) {
        auto snapshot = std::make_shared<magda::engine::ClipSnapshot>();
        snapshot->serial = serial;
        snapshot->tempoFingerprint = tempo.fingerprint();
        magda::engine::TrackClipPlayback track;
        track.trackId = kTrack;
        track.audio.push_back(clipAt(pitch));
        snapshot->tracks.push_back(std::move(track));
        return snapshot;
    };

    SineFiles files;
    magda::engine::PrefetchThread reader(false);
    magda::engine::ClipVoicePool pool(files, reader, rate);
    pool.setPrimesStandbys(play.standbys);

    auto snapshot = snapshotOf(0.0f, kSnapshot);
    pool.setSnapshot(snapshot);
    magda::engine::ClipSnapshotFeed clips;
    clips.publish(snapshot);

    const auto secondsAt = [](int sample) { return kEventStart + sample / kSampleRate; };
    pool.setTransport(
        magda::engine::LoopRange{
            play.loop, tempo.timeToBeat(secondsAt(play.loopOffsetSamples)),
            tempo.timeToBeat(secondsAt(play.loopOffsetSamples + play.loopSamples))},
        tempo);

    magda::engine::ClipAudioSource source{kTrack, clips, pool.feed()};
    source.prepare(rate);

    if (play.cursorSamples >= 0)
        for (auto round = 0; round < 3; ++round) {
            pool.setPosition(secondsAt(play.cursorSamples), false);
            pool.service();
            pool.fillNow();
        }

    LoopedRun run;
    juce::AudioBuffer<float> out(2, play.blockSize);
    const auto loopEnd = play.loopOffsetSamples + play.loopSamples;
    auto position = play.cursorSamples >= 0 ? play.cursorSamples : play.loopOffsetSamples;
    auto continuous = play.cursorSamples < 0;
    for (auto index = 0; index < play.loops * (play.loopSamples / play.blockSize); ++index) {
        if (index == play.pitchEditAt) {
            snapshot = snapshotOf(3.0f, kSnapshot + 1);
            pool.setSnapshot(snapshot);
            clips.publish(snapshot);
        }

        // Cut at the loop's end, where the transport splits a block.
        const auto samples = play.loop && position < loopEnd
                                 ? std::min(play.blockSize, loopEnd - position)
                                 : play.blockSize;
        const auto at = secondsAt(position);

        pool.setPosition(at);
        pool.service();
        pool.fillNow();

        BlockInfo block;
        block.numSamples = samples;
        block.playing = true;
        block.continuous = continuous;
        block.seconds = {at, at + samples / kSampleRate};
        block.beats = {tempo.timeToBeat(block.seconds.start), tempo.timeToBeat(block.seconds.end)};
        block.tempo = &tempo;

        out.clear();
        const auto lane =
            juce::dsp::AudioBlock<float>(out).getSubBlock(0, static_cast<std::size_t>(samples));
        magda::test::renderBlock(source, clips, block, lane, nullptr, &pool.feed());
        for (auto channel = 0; channel < 2; ++channel)
            run.rendered.insert(run.rendered.end(), out.getReadPointer(channel),
                                out.getReadPointer(channel) + samples);

        position += samples;
        continuous = true;
        if (play.loop && position == loopEnd) {
            position = play.loopOffsetSamples;
            continuous = false;
            ++run.wraps;
        }
    }

    run.taken = pool.standbysTaken();
    return run;
}

/// The run with standbys and the one priming every start in the callback, which must agree.
std::pair<LoopedRun, LoopedRun> bothWays(LoopedPlay play) {
    play.standbys = false;
    auto primed = playLooped(play);
    play.standbys = true;
    return {primed, playLooped(play)};
}

/// SineFiles, counting every sample read out of the files it opened.
class CountingSineFiles final : public magda::engine::AudioFileReaderFactory {
  public:
    std::unique_ptr<magda::engine::AudioFileReader> open(const std::string&) override {
        return std::make_unique<Counting>(read);
    }

    std::atomic<std::int64_t> read{0};

  private:
    class Counting final : public magda::engine::AudioFileReader {
      public:
        explicit Counting(std::atomic<std::int64_t>& read) : read_(read) {}

        std::int64_t lengthInSamples() const override {
            return sine_.lengthInSamples();
        }
        double sampleRate() const override {
            return sine_.sampleRate();
        }
        int numChannels() const override {
            return sine_.numChannels();
        }
        int read(juce::AudioBuffer<float>& destination, int destinationOffset,
                 std::int64_t startSample, int numSamples) override {
            read_.fetch_add(numSamples, std::memory_order_relaxed);
            return sine_.read(destination, destinationOffset, startSample, numSamples);
        }

      private:
        SineReader sine_;
        std::atomic<std::int64_t>& read_;
    };
};

struct HandBackRun {
    std::vector<float> rendered;
    int taken = 0;
    std::int64_t readWhileHeld = 0;
    float heldPeak = 0.0f;
    float resumedPeak = 0.0f;
};

/// A stretched arrangement clip a slot takes over, with a release queued twenty blocks before
/// it lands mid-block, through the real voice pool (#2787).
HandBackRun playHandBack(bool standbys) {
    constexpr magda::TrackId kTrack = 3;
    constexpr int kHoldAt = 10;
    constexpr int kHeldQuietFrom = 12;
    constexpr int kReleaseAskedAt = 20;
    constexpr int kBlocks = 60;
    constexpr int kReleaseSample = 40 * kBlockSize + 100;
    const TempoMap tempo;

    Rig shape;
    auto snapshot = std::make_shared<magda::engine::ClipSnapshot>();
    snapshot->serial = kSnapshot;
    snapshot->tempoFingerprint = tempo.fingerprint();
    magda::engine::TrackClipPlayback track;
    track.trackId = kTrack;
    track.audio.push_back(shape.clip);
    snapshot->tracks.push_back(std::move(track));

    CountingSineFiles files;
    magda::engine::PrefetchThread reader(false);
    magda::engine::ClipVoicePool pool(files, reader, context());
    pool.setPrimesStandbys(standbys);
    pool.setSnapshot(snapshot);
    pool.setTransport(magda::engine::LoopRange{}, tempo);
    magda::engine::ClipSnapshotFeed clips;
    clips.publish(snapshot);

    magda::engine::LaunchHandle handle;
    magda::engine::LaunchHandleTable table;
    table.entries.push_back({.key = magda::engine::SlotKey{kTrack, 0}, .handle = &handle});
    magda::engine::LaunchHandleFeed handles;
    handles.publish(std::make_shared<const magda::engine::LaunchHandleTable>(table));
    magda::engine::LaunchRequestQueue requests;

    magda::engine::ClipAudioSource source{kTrack, clips, pool.feed(), handles,
                                          magda::engine::Section::Arrangement};
    source.prepare(context());

    const auto secondsAt = [](int sample) { return kEventStart + sample / kSampleRate; };

    HandBackRun run;
    juce::AudioBuffer<float> out(2, kBlockSize);
    for (auto index = 0; index < kBlocks; ++index) {
        if (index == kHoldAt)
            handle.play(std::nullopt);
        if (index == kReleaseAskedAt)
            handle.releaseSection(tempo.timeToBeat(secondsAt(kReleaseSample)));
        if (index == kHeldQuietFrom)
            run.readWhileHeld = -files.read.load();
        if (index == kReleaseAskedAt)
            run.readWhileHeld += files.read.load();

        const auto at = secondsAt(index * kBlockSize);
        pool.setPosition(at);
        pool.service();
        pool.fillNow();

        BlockInfo block;
        block.numSamples = kBlockSize;
        block.sampleRate = kSampleRate;
        block.playing = true;
        block.continuous = index > 0;
        block.seconds = {at, at + kBlockSize / kSampleRate};
        block.beats = {tempo.timeToBeat(block.seconds.start), tempo.timeToBeat(block.seconds.end)};
        block.monotonicBeats = block.beats;
        block.monotonicSeconds = block.seconds;
        block.monotonicSamples = {magda::engine::SamplePosition{index * kBlockSize},
                                  magda::engine::SamplePosition{(index + 1) * kBlockSize}};
        block.tempo = &tempo;

        out.clear();
        {
            const magda::engine::ClipSnapshotFeed::BlockScope pinned(clips);
            const magda::engine::ClipStreamFeed::BlockScope streams(pool.feed());
            magda::engine::advanceLaunchHandles(handles, requests, block);
            magda::engine::advanceTrackSections(clips.sections(), clips.live(), &handles, block);
            pool.announceHandBacks(clips.sections(), block);
            source.render(block, juce::dsp::AudioBlock<float>(out));
        }

        const auto peak = out.getMagnitude(0, kBlockSize);
        if (index > kHoldAt && index < kReleaseSample / kBlockSize)
            run.heldPeak = std::max(run.heldPeak, peak);
        if (index > kReleaseSample / kBlockSize)
            run.resumedPeak = std::max(run.resumedPeak, peak);
        for (auto channel = 0; channel < 2; ++channel)
            run.rendered.insert(run.rendered.end(), out.getReadPointer(channel),
                                out.getReadPointer(channel) + kBlockSize);
    }

    run.taken = pool.standbysTaken();
    return run;
}

float loudest(const std::vector<float>& samples) {
    auto peak = 0.0f;
    for (const auto sample : samples)
        peak = std::max(peak, std::abs(sample));
    return peak;
}

}  // namespace

TEST_CASE("A start that takes a standby renders what priming it in the callback renders",
          "[engine][clip][stretch][2786]") {
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 40, primed.active.get());

    Rig adopted;
    const auto standby = adopted.standbyAt(kEventStart);
    REQUIRE(standby != nullptr);
    adopted.render(kEventStart, false, adopted.active.get(), standby.get());
    adopted.roll(0, 40, adopted.active.get(), standby.get());

    CHECK(adopted.voice.adoptions() == 1);
    CHECK(standby->claimed.load());
    REQUIRE(loudest(primed.rendered) > 0.01f);
    CHECK(adopted.rendered == primed.rendered);
}

TEST_CASE("A voice carries on through the pool making its standby the entry's stretcher",
          "[engine][clip][stretch][2786]") {
    // The table after the one it was claimed from names it as the entry's own and no longer as a
    // standby, and the voice is still rendering the same instance.
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 40, primed.active.get());

    Rig adopted;
    const auto standby = adopted.standbyAt(kEventStart);
    REQUIRE(standby != nullptr);
    adopted.render(kEventStart, false, adopted.active.get(), standby.get());
    adopted.roll(0, 10, adopted.active.get(), standby.get());
    adopted.roll(10, 30, standby->stretcher.get());

    CHECK(adopted.voice.adoptions() == 1);
    CHECK(adopted.rendered == primed.rendered);
}

TEST_CASE("A claimed standby is not taken by a second start", "[engine][clip][stretch][2786]") {
    // A short loop can come round again before the pool publishes the next table. The standby
    // still named there has played since it was primed, so the second start primes in the
    // callback, on the instance the voice is using, exactly as it would have without one.
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 30, primed.active.get());
    primed.recue(kEventStart);
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 30, primed.active.get());

    Rig adopted;
    const auto standby = adopted.standbyAt(kEventStart);
    REQUIRE(standby != nullptr);
    adopted.render(kEventStart, false, adopted.active.get(), standby.get());
    adopted.roll(0, 30, adopted.active.get(), standby.get());
    adopted.recue(kEventStart);
    adopted.render(kEventStart, false, adopted.active.get(), standby.get());
    adopted.roll(0, 30, adopted.active.get(), standby.get());

    CHECK(adopted.voice.adoptions() == 1);
    CHECK(adopted.rendered == primed.rendered);
}

TEST_CASE("A fresh standby stands in for re-priming an instance that has played",
          "[engine][clip][stretch][2786]") {
    // What a loop return is: the voice's stretcher has rendered a long stretch, and the pool
    // hands it a new instance primed for the loop's start instead.
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 200, primed.active.get());
    primed.recue(kEventStart);
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 30, primed.active.get());

    Rig adopted;
    adopted.render(kEventStart, false, adopted.active.get(), nullptr);
    adopted.roll(0, 200, adopted.active.get());
    const auto standby = adopted.standbyAt(kEventStart);
    REQUIRE(standby != nullptr);
    adopted.recue(kEventStart);
    adopted.render(kEventStart, false, adopted.active.get(), standby.get());
    adopted.roll(0, 30, adopted.active.get(), standby.get());

    CHECK(adopted.voice.adoptions() == 1);
    CHECK(adopted.rendered == primed.rendered);
}

TEST_CASE("A standby primed under another snapshot is left alone",
          "[engine][clip][stretch][2786]") {
    // A pitch or source edit leaves the priming inputs where they were and changes the snapshot.
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 20, primed.active.get());

    Rig refused;
    const auto stale = refused.standbyAt(kEventStart, kSnapshot + 1);
    REQUIRE(stale != nullptr);
    refused.render(kEventStart, false, refused.active.get(), stale.get());
    refused.roll(0, 20, refused.active.get(), stale.get());

    CHECK(refused.voice.adoptions() == 0);
    CHECK_FALSE(stale->claimed.load());
    CHECK(refused.rendered == primed.rendered);
}

TEST_CASE("A standby that arrives after its start leaves the sounding instance alone",
          "[engine][clip][stretch][2786]") {
    // Preparation was late, so the start primed in the callback. The standby then published for
    // it must not be taken, reset or swapped under a voice that is carrying on.
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 40, primed.active.get());

    Rig late;
    late.render(kEventStart, false, late.active.get(), nullptr);
    late.roll(0, 5, late.active.get());
    const auto standby = late.standbyAt(kEventStart);
    REQUIRE(standby != nullptr);
    late.roll(5, 35, late.active.get(), standby.get());

    CHECK(late.voice.adoptions() == 0);
    CHECK_FALSE(standby->claimed.load());
    CHECK(late.rendered == primed.rendered);
}

TEST_CASE("A fresh Signalsmith instance primes to the state a played one resets to",
          "[engine][clip][stretch][2786]") {
    // What lets the pool prime a new instance for a start rather than the one that played up to
    // it: after priming, nothing it rendered before survives, reset or not.
    Rig rig;
    const auto key = rig.keyAt(kEventStart);
    juce::AudioBuffer<float> window(2, key.preRoll);
    rig.source()->read(window, 0, key.readFrom - key.preRoll, key.preRoll);

    constexpr int kReading = 192;
    constexpr int kCells = 20;
    juce::AudioBuffer<float> input(2, kReading * kCells);
    rig.source()->read(input, 0, key.readFrom, input.getNumSamples());

    const auto playElsewhere = [&](ClipStretcher& stretcher) {
        juce::AudioBuffer<float> elsewhere(2, kReading);
        juce::AudioBuffer<float> discarded(2, magda::engine::kStretchCellSamples);
        for (auto cell = 0; cell < 300; ++cell) {
            rig.source()->read(elsewhere, 0, 100000 + cell * kReading, kReading);
            stretcher.process(juce::dsp::AudioBlock<const float>(elsewhere), 0.0, 1.5,
                              juce::dsp::AudioBlock<float>(discarded));
        }
    };

    const auto primeAndRun = [&](ClipStretcher& stretcher) {
        REQUIRE(stretcher.primeFromWindow(juce::dsp::AudioBlock<const float>(window), key.step));
        std::vector<float> rendered;
        juce::AudioBuffer<float> cell(2, magda::engine::kStretchCellSamples);
        const juce::dsp::AudioBlock<const float> reading(input);
        for (auto index = 0; index < kCells; ++index) {
            stretcher.process(reading.getSubBlock(static_cast<std::size_t>(index * kReading),
                                                  static_cast<std::size_t>(kReading)),
                              0.0, key.step, juce::dsp::AudioBlock<float>(cell));
            rendered.insert(rendered.end(), cell.getReadPointer(0),
                            cell.getReadPointer(0) + cell.getNumSamples());
        }
        return rendered;
    };

    auto fresh = magda::engine::makeStretcher(rig.setup);
    const auto expected = primeAndRun(*fresh);
    REQUIRE(loudest(expected) > 0.01f);

    auto reset = magda::engine::makeStretcher(rig.setup);
    playElsewhere(*reset);
    reset->reset();
    CHECK(primeAndRun(*reset) == expected);

    auto played = magda::engine::makeStretcher(rig.setup);
    playElsewhere(*played);
    CHECK(primeAndRun(*played) == expected);
}

TEST_CASE("A loop's returns into a stretched clip take standbys and sound as they did",
          "[engine][clip][stretch][2786]") {
    // Through the pool itself: it sees each return coming, primes for it, the voice takes it,
    // and the pool makes it the entry's stretcher before priming the next.
    for (const auto blockSize : {64, 256, 512}) {
        INFO("block size " << blockSize);
        LoopedPlay play;
        play.blockSize = blockSize;
        const auto [primed, standbys] = bothWays(play);

        CHECK(primed.taken == 0);
        CHECK(standbys.taken >= play.loops - 1);
        REQUIRE(loudest(primed.rendered) > 0.01f);
        CHECK(standbys.rendered == primed.rendered);
    }
}

TEST_CASE("A loop longer than the clip it returns into primes for the return",
          "[engine][clip][stretch][2786]") {
    // Near the loop's end the clip is long over, held only because the loop comes back into it,
    // and the start being prepared is that return rather than the clip's own beginning.
    LoopedPlay play;
    play.clipSamples = 80 * kBlockSize;
    play.loopOffsetSamples = 20 * kBlockSize;
    play.loopSamples = 120 * kBlockSize;
    const auto [primed, standbys] = bothWays(play);

    CHECK(standbys.taken >= play.loops - 1);
    REQUIRE(loudest(primed.rendered) > 0.01f);
    CHECK(standbys.rendered == primed.rendered);
}

TEST_CASE("A pitch edit after a standby was taken plays the new pitch as priming would",
          "[engine][clip][stretch][2786]") {
    // The edit lands the block after the return, before the pool has made the taken standby the
    // entry's own: it was built under the old setup, so it must not be put in the new one's place.
    LoopedPlay play;
    play.pitchEditAt = play.loopSamples / play.blockSize * 2 + 1;
    const auto [primed, standbys] = bothWays(play);

    CHECK(standbys.taken >= play.loops - 2);
    REQUIRE(loudest(primed.rendered) > 0.01f);
    CHECK(standbys.rendered == primed.rendered);
}

TEST_CASE("Starting from a stopped cursor takes the standby primed for where play lands",
          "[engine][clip][stretch][2786]") {
    // Stopped, the next start is the cursor's: at a clip's first sample, or inside one.
    for (const auto cursor : {0, 40 * kBlockSize + 77}) {
        INFO("cursor " << cursor);
        LoopedPlay play;
        play.loop = false;
        play.cursorSamples = cursor;
        play.loops = 1;
        const auto [primed, standbys] = bothWays(play);

        CHECK(standbys.taken == 1);
        // Sounding from the first block: the stream is cued where the voice's prime reads from.
        const std::vector<float> opening(primed.rendered.begin(),
                                         primed.rendered.begin() + 2 * kBlockSize);
        REQUIRE(loudest(opening) > 0.01f);
        CHECK(standbys.rendered == primed.rendered);
    }
}

TEST_CASE("Play from a stopped cursor inside a loop takes the cursor's standby, then the returns",
          "[engine][clip][stretch][2786]") {
    // The pool's next start once play begins is the loop's return, and the cursor's standby is
    // still the one the first block needs.
    LoopedPlay play;
    play.cursorSamples = play.loopOffsetSamples + 20 * kBlockSize + 77;
    play.loops = 3;
    const auto [primed, standbys] = bothWays(play);

    REQUIRE(standbys.wraps >= 2);
    CHECK(standbys.taken == standbys.wraps + 1);
    REQUIRE(loudest(primed.rendered) > 0.01f);
    CHECK(standbys.rendered == primed.rendered);
}

TEST_CASE("A standby goes to whichever of the voice and the pool claims it first",
          "[engine][clip][stretch][2786]") {
    // The table a voice reads can still name a standby the pool has since withdrawn, or one the
    // pool is withdrawing as the voice takes it; the claim decides, in either order.
    Rig primed;
    primed.render(kEventStart, false, primed.active.get(), nullptr);
    primed.roll(0, 30, primed.active.get());

    SECTION("withdrawn before the start primes in the callback") {
        Rig withdrawn;
        const auto standby = withdrawn.standbyAt(kEventStart);
        REQUIRE(standby != nullptr);
        REQUIRE(standby->claim());
        withdrawn.render(kEventStart, false, withdrawn.active.get(), standby.get());
        withdrawn.roll(0, 5, withdrawn.active.get(), standby.get());
        withdrawn.roll(5, 25, withdrawn.active.get());

        CHECK(withdrawn.voice.adoptions() == 0);
        CHECK(withdrawn.rendered == primed.rendered);
    }

    SECTION("taken before the withdrawal is kept and promoted") {
        Rig taken;
        const auto standby = taken.standbyAt(kEventStart);
        REQUIRE(standby != nullptr);
        taken.render(kEventStart, false, taken.active.get(), standby.get());
        CHECK_FALSE(standby->claim());
        taken.roll(0, 5, taken.active.get(), standby.get());
        taken.roll(5, 25, standby->stretcher.get());

        CHECK(taken.voice.adoptions() == 1);
        CHECK(taken.rendered == primed.rendered);
    }
}

TEST_CASE("A held track reads nothing, and a quantized hand-back takes a standby",
          "[engine][clip][stretch][session][2787]") {
    const auto primed = playHandBack(false);
    const auto standbys = playHandBack(true);

    CHECK(standbys.readWhileHeld == 0);
    CHECK(standbys.heldPeak == 0.0f);
    CHECK(standbys.taken == 1);
    CHECK(primed.taken == 0);
    REQUIRE(standbys.resumedPeak > 0.01f);
    CHECK(standbys.rendered == primed.rendered);
}
