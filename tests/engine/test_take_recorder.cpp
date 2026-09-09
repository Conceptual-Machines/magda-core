#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/TakeRecorder.hpp"
#include "launch/SessionLauncher.hpp"
#include "tap/RecordTap.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TransportClock.hpp"
#include "transport/TransportState.hpp"

/**
 * @file test_take_recorder.cpp
 * @brief An arrangement audio take, from armed to clip (#2461).
 *
 * A synthetic input and a driven transport, with no session and no thread: the
 * take is fed a callback at a time and drained when it is closed, so a case
 * says exactly where the transport was when a sample arrived.
 *
 * Every input sample says which arrival it is, which is what lets a case assert
 * on where a take begins rather than only on how long it is.
 *
 * A slot take is driven the same way, with one published handle the launcher
 * advances over each block before the take sees it (#2464).
 */

using magda::engine::advanceLaunchHandles;
using magda::engine::AudioFileFormat;
using magda::engine::BlockInfo;
using magda::engine::LaunchHandle;
using magda::engine::LaunchHandleFeed;
using magda::engine::LaunchHandleTable;
using magda::engine::LaunchRequestQueue;
using magda::engine::LiveInputBlock;
using magda::engine::LiveInputFeed;
using magda::engine::LoopRange;
using magda::engine::RecordedTake;
using magda::engine::RenderContext;
using magda::engine::SlotKey;
using magda::engine::SlotRunTarget;
using magda::engine::TakeRecorder;
using magda::engine::TakeRecorderSettings;
using magda::engine::TempoMap;
using magda::engine::TransportClock;
using magda::engine::TransportSnapshot;

namespace {

constexpr double kSampleRate = 8000.0;
constexpr int kBlockSize = 64;

/// A beat at 120 bpm and this rate, which makes every position in these cases
/// a whole number of samples.
constexpr int kBeatSamples = 4000;

/// What arrival @p sample of @p channel carries. Inside full scale, and spaced
/// far enough apart that two arrivals cannot round onto one float.
float material(std::int64_t sample, int channel) {
    return static_cast<float>(sample + 1 + (static_cast<std::int64_t>(channel) * 50000)) * 1.0e-5f;
}

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_take_recorder_test");
    root.createDirectory();
    return root;
}

/// A directory of its own, so a case can say what is left in it.
juce::File emptyDirectory(const juce::String& name) {
    auto directory = scratch().getChildFile(name);
    directory.deleteRecursively();
    directory.createDirectory();
    return directory;
}

juce::AudioBuffer<float> readBack(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

/// A take with nothing to round: float files hold what they were handed, so a
/// case can compare against the input itself.
TakeRecorderSettings floatTake(std::vector<int> channels, int latencySamples = 0) {
    TakeRecorderSettings settings;
    settings.channels = std::move(channels);
    settings.latencySamples = latencySamples;
    settings.name = "take";
    settings.file.format = AudioFileFormat::wav;
    settings.file.bitDepth = 32;
    return settings;
}

/**
 * @brief One slot's handle, published the way the session publishes them.
 *
 * What a take follows instead of the transport (#2464): a launch is asked for
 * on a monotonic beat and fires wherever that beat lands, which is what a case
 * needs to be inside a block rather than on its edge.
 */
class SlotLaunch {
  public:
    SlotLaunch() {
        auto table = std::make_shared<LaunchHandleTable>();
        table->entries.push_back(
            LaunchHandleTable::Entry{.key = kKey, .handle = &handle_, .incarnation = kIncarnation});
        feed_.publish(std::move(table));

        // What the store does beside the publish: a request stamped with
        // anything else is dropped as a slot that has been refilled.
        requests_.setIncarnations({{kKey, kIncarnation}});
    }

    void launch(double monotonicBeat) {
        LaunchRequestQueue::Gesture gesture(requests_);
        gesture.play(kKey, monotonicBeat);
    }

    void stop(double monotonicBeat) {
        LaunchRequestQueue::Gesture gesture(requests_);
        gesture.stop(kKey, monotonicBeat);
    }

    /// What a take made for this slot follows.
    SlotRunTarget target() {
        return {.handles = &feed_, .key = kKey, .incarnation = kIncarnation};
    }

    /// The launcher's own pass over the block, before anything renders it.
    void advance(const BlockInfo& block) {
        advanceLaunchHandles(feed_, requests_, block);
    }

  private:
    static constexpr SlotKey kKey{1, 0};
    static constexpr std::uint64_t kIncarnation = 1;

    LaunchHandle handle_;
    LaunchHandleFeed feed_;
    LaunchRequestQueue requests_;
};

/**
 * @brief A transport, an input and one take, driven a callback at a time.
 */
class Rig {
  public:
    Rig(const juce::File& directory, TakeRecorderSettings settings, int inputChannels = 2,
        TempoMap tempo = TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}}))
        : input_(inputChannels, kBlockSize) {
        transport_.tempo = std::move(tempo);
        feed_.prepare(inputChannels, kBlockSize);

        settings.directory = directory;
        recorder_ = std::make_unique<TakeRecorder>(
            feed_, RenderContext{kSampleRate, kBlockSize, inputChannels}, tap_,
            std::move(settings));
    }

    void play(double fromBeat = 0.0, double countInBeats = 0.0) {
        ++transport_.request.generation;
        transport_.request.playing = true;
        transport_.request.locate = true;
        transport_.request.positionBeat = fromBeat;
        transport_.request.countInBeats = countInBeats;
    }

    void stop() {
        ++transport_.request.generation;
        transport_.request.playing = false;
        transport_.request.locate = false;
        transport_.request.countInBeats = 0.0;
    }

    void loop(double startBeat, double endBeat) {
        transport_.loop = LoopRange{true, startBeat, endBeat};
    }

    void unloop() {
        transport_.loop = {};
    }

    /// Feed @p numSamples of callbacks through the take.
    void run(int numSamples) {
        for (auto left = numSamples; left > 0;) {
            const auto callback = std::min(kBlockSize, left);
            deliver(callback);
            left -= callback;
        }
    }

    /// Empty the queue after every callback, the way the record thread would.
    /// What a take holds must not depend on it.
    void drainAsItGoes() {
        drains_ = true;
    }

    /// Advance @p launch over every block before the take sees it, which is the
    /// order the audio thread keeps (#2464).
    void follows(SlotLaunch& launch) {
        launch_ = &launch;
    }

    TakeRecorder& recorder() {
        return *recorder_;
    }

  private:
    void deliver(int numSamples) {
        for (auto channel = 0; channel < input_.getNumChannels(); ++channel)
            for (auto at = 0; at < numSamples; ++at)
                input_.setSample(channel, at, material(arrival_ + at, channel));

        const LiveInputBlock block{juce::dsp::AudioBlock<const float>(input_).getSubBlock(
                                       0, static_cast<std::size_t>(numSamples)),
                                   {}};

        feed_.beginCallback(block, numSamples);

        for (const auto& segment : clock_.advance(transport_, kSampleRate, numSamples)) {
            feed_.beginSegment(segment.startSample, segment.block.numSamples);

            if (launch_ != nullptr)
                launch_->advance(segment.block);

            recorder_->capture(segment.block, segment.countingIn, transport_.loop);
        }

        feed_.endCallback();
        arrival_ += numSamples;

        if (drains_)
            while (recorder_->stream().drain()) {
            }
    }

    TransportSnapshot transport_;
    TransportClock clock_;
    LiveInputFeed feed_;
    juce::AudioBuffer<float> input_;

    /// Nothing here draws the pass; #2463 is where that is covered.
    magda::engine::RecordTap tap_{magda::engine::RecordMaterial::audio, {}};

    std::unique_ptr<TakeRecorder> recorder_;

    /// Input samples delivered since the rig was made, which is what the
    /// material is numbered by.
    std::int64_t arrival_ = 0;

    /// The slot the take follows, for a case that has one.
    SlotLaunch* launch_ = nullptr;

    bool drains_ = false;
};

/// The arrival the take's first sample came from, read out of the file itself.
std::int64_t firstArrival(const juce::AudioBuffer<float>& stored) {
    return std::llround(static_cast<double>(stored.getSample(0, 0)) * 1.0e5) - 1;
}

/// The same for any sample of it, which is what says a take holds one
/// unbroken stretch of the input.
std::int64_t arrivalAt(const juce::AudioBuffer<float>& stored, int at) {
    return std::llround(static_cast<double>(stored.getSample(0, at)) * 1.0e5) - 1;
}

/// A take of @p launch's run rather than of the transport.
TakeRecorderSettings slotTake(SlotLaunch& launch, int latencySamples = 0) {
    auto settings = floatTake({0, 1}, latencySamples);
    settings.slot = launch.target();
    return settings;
}

}  // namespace

TEST_CASE("A take holds the input the device delivered, sample for sample",
          "[engine][io][record][2461]") {
    Rig rig(emptyDirectory("plain"), floatTake({0, 1}));
    rig.play();
    rig.run(256);

    const auto take = rig.recorder().finish();
    REQUIRE_FALSE(take.empty());
    CHECK(take.samplesLost == 0);
    CHECK_FALSE(take.failed);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumChannels() == 2);
    REQUIRE(stored.getNumSamples() == 256);

    for (auto channel = 0; channel < 2; ++channel)
        for (auto at = 0; at < stored.getNumSamples(); ++at) {
            INFO("channel " << channel << " sample " << at);
            REQUIRE(stored.getSample(channel, at) == material(at, channel));
        }

    // A single pass is an ordinary clip: the take list is for loop-record
    // alternatives and stays empty without them.
    CHECK(take.clip.takes.empty());
    CHECK(take.startBeat == 0.0);
    CHECK(take.lengthBeats == Catch::Approx(256.0 / kBeatSamples));
}

TEST_CASE("The take's first sample is the one under the playhead at record start",
          "[engine][io][record][2461]") {
    SECTION("a positive latency drops what arrived before it") {
        Rig rig(emptyDirectory("latency_positive"), floatTake({0, 1}, 128));
        rig.play();
        rig.run(256);

        const auto stored = readBack(rig.recorder().finish().file);
        CHECK(stored.getNumSamples() == 128);
        CHECK(firstArrival(stored) == 128);
    }

    SECTION("a negative latency stands silence in for what never arrived") {
        Rig rig(emptyDirectory("latency_negative"), floatTake({0, 1}, -128));
        rig.play();
        rig.run(256);

        const auto stored = readBack(rig.recorder().finish().file);
        REQUIRE(stored.getNumSamples() == 384);

        for (auto at = 0; at < 128; ++at) {
            INFO("sample " << at);
            REQUIRE(stored.getSample(0, at) == 0.0f);
        }

        CHECK(stored.getSample(0, 128) == material(0, 0));
    }

    SECTION("no latency keeps the first arrival") {
        Rig rig(emptyDirectory("latency_none"), floatTake({0, 1}));
        rig.play();
        rig.run(256);

        const auto stored = readBack(rig.recorder().finish().file);
        CHECK(stored.getNumSamples() == 256);
        CHECK(firstArrival(stored) == 0);
    }
}

TEST_CASE("A count-in is not part of the take", "[engine][io][record][2461]") {
    Rig rig(emptyDirectory("count_in"), floatTake({0, 1}));
    rig.play(1.0, 1.0);
    rig.run(kBeatSamples + 256);

    const auto stored = readBack(rig.recorder().finish().file);
    CHECK(stored.getNumSamples() == 256);
    CHECK(firstArrival(stored) == kBeatSamples);
}

TEST_CASE("Loop recording is one file a pass, loop-aligned", "[engine][io][record][2461]") {
    const auto directory = emptyDirectory("loop_passes");

    Rig rig(directory, floatTake({0, 1}));
    rig.loop(0.0, 2.0);
    rig.play();
    rig.run(3 * 2 * kBeatSamples);

    const auto take = rig.recorder().finish();
    REQUIRE(take.clip.takes.size() == 3);

    for (const auto& pass : take.clip.takes) {
        INFO(pass.filePath);
        CHECK(pass.durationSeconds == Catch::Approx(1.0));
        CHECK(readBack(juce::File(pass.filePath)).getNumSamples() == 2 * kBeatSamples);
    }

    // Every pass starts where the loop does, which is where the clip goes.
    CHECK(take.startBeat == 0.0);
    CHECK(take.lengthBeats == Catch::Approx(2.0));
    CHECK(take.clip.currentTakeIndex == 2);

    CHECK(directory.getNumberOfChildFiles(juce::File::findFiles) == 3);
}

TEST_CASE("The active take is the last full pass", "[engine][io][record][2461]") {
    SECTION("a pass cut off mid-way is not the one that plays") {
        Rig rig(emptyDirectory("short_final"), floatTake({0, 1}));
        rig.loop(0.0, 2.0);
        rig.play();
        rig.run((3 * 2 * kBeatSamples) + 2000);

        const auto take = rig.recorder().finish();
        REQUIRE(take.clip.takes.size() == 4);
        CHECK(take.clip.takes.back().durationSeconds == Catch::Approx(0.25));
        CHECK(take.clip.currentTakeIndex == 2);
        CHECK(take.file.getFullPathName() == take.clip.takes[2].filePath);
    }

    SECTION("a final pass that ran its length is the one that plays") {
        Rig rig(emptyDirectory("full_final"), floatTake({0, 1}));
        rig.loop(0.0, 2.0);
        rig.play();
        rig.run(2 * 2 * kBeatSamples);

        const auto take = rig.recorder().finish();
        REQUIRE(take.clip.takes.size() == 2);
        CHECK(take.clip.currentTakeIndex == 1);
    }
}

TEST_CASE("Head padding does not make a whole final pass look short",
          "[engine][io][record][2461]") {
    // The first pass carries the padding a negative adjustment put at the head,
    // so it is longer than the loop. Judged against that, a final pass that ran
    // its full length would be taken for a stop.
    Rig rig(emptyDirectory("padded_first_pass"), floatTake({0, 1}, -512));
    rig.loop(0.0, 2.0);
    rig.play();
    rig.run(3 * 2 * kBeatSamples);

    const auto take = rig.recorder().finish();
    REQUIRE(take.clip.takes.size() == 3);
    CHECK(readBack(juce::File(take.clip.takes[0].filePath)).getNumSamples() ==
          (2 * kBeatSamples) + 512);
    CHECK(readBack(juce::File(take.clip.takes[2].filePath)).getNumSamples() == 2 * kBeatSamples);

    CHECK(take.clip.currentTakeIndex == 2);
    CHECK(take.file.getFullPathName() == take.clip.takes[2].filePath);
}

TEST_CASE("A lead-in recorded before the loop is not a take", "[engine][io][record][2461]") {
    const auto directory = emptyDirectory("lead_in");

    Rig rig(directory, floatTake({0, 1}));
    rig.loop(0.0, 2.0);
    rig.play(1.0);
    rig.run(kBeatSamples + (2 * kBeatSamples));

    const auto take = rig.recorder().finish();

    // The lead-in cannot share the clip start the other passes do, so it is
    // dropped rather than kept as a take that plays in the wrong place.
    CHECK(take.clip.takes.empty());
    CHECK(readBack(take.file).getNumSamples() == 2 * kBeatSamples);
    CHECK(take.startBeat == 0.0);
    CHECK(take.lengthBeats == Catch::Approx(2.0));
    CHECK(directory.getNumberOfChildFiles(juce::File::findFiles) == 1);
}

TEST_CASE("A lead-in kept for want of a whole pass stays where it was recorded",
          "[engine][io][record][2461]") {
    // The wrap has been seen, but the samples belonging to it are still a
    // latency away when the transport stops, so the only pass captured is the
    // lead-in. Placing that at the loop start would play it a beat early.
    Rig rig(emptyDirectory("lead_in_only"), floatTake({0, 1}, 128));
    rig.loop(0.0, 2.0);
    rig.play(1.0);
    rig.run(kBeatSamples + 64);

    const auto take = rig.recorder().finish();
    CHECK(take.clip.takes.empty());
    CHECK(take.startBeat == Catch::Approx(1.0));
    CHECK(take.lengthBeats == Catch::Approx((kBeatSamples + 64 - 128.0) / kBeatSamples));
    CHECK(readBack(take.file).getNumSamples() == kBeatSamples + 64 - 128);
}

TEST_CASE("A loop shorter than the input latency still splits every pass",
          "[engine][io][record][2461]") {
    // Every wrap is a pass end that has not arrived yet, so several are
    // outstanding at once. A boundary counted down to rather than named would
    // be replaced by the next wrap and never reached.
    constexpr int kLoopSamples = 64;
    constexpr int kLatency = 128;

    Rig rig(emptyDirectory("short_loop"), floatTake({0, 1}, kLatency));
    rig.loop(0.0, static_cast<double>(kLoopSamples) / kBeatSamples);
    rig.play();
    rig.run(10 * kLoopSamples);

    const auto take = rig.recorder().finish();

    // Ten loops delivered, less the two the head correction gave up.
    REQUIRE(take.clip.takes.size() == 8);
    for (const auto& pass : take.clip.takes)
        CHECK(readBack(juce::File(pass.filePath)).getNumSamples() == kLoopSamples);
}

TEST_CASE("A pass holds the same samples however often the disk is drained",
          "[engine][io][record][2461]") {
    // A negative adjustment queues a pass's last samples before the transport
    // wraps, so a boundary named at the wrap would already be behind them and
    // the pass would end wherever the writer had got to.
    const auto passSamples = [](bool drains) {
        Rig rig(emptyDirectory(drains ? "drained_each" : "drained_late"), floatTake({0, 1}, -128));
        rig.loop(0.0, 2.0);
        rig.play();

        if (drains)
            rig.drainAsItGoes();

        rig.run(3 * 2 * kBeatSamples);

        const auto take = rig.recorder().finish();
        std::vector<int> lengths;
        for (const auto& pass : take.clip.takes)
            lengths.push_back(readBack(juce::File(pass.filePath)).getNumSamples());
        return lengths;
    };

    const auto late = passSamples(false);
    const auto each = passSamples(true);

    REQUIRE(late == each);
    REQUIRE_FALSE(late.empty());

    // Three loops delivered, so three passes. The first runs long by the head
    // padding, because a boundary is never named behind audio already queued
    // and a negative adjustment queues a pass's tail before the wrap; every
    // pass after it is the loop.
    REQUIRE(late.size() == 3);
    CHECK(late.front() == (2 * kBeatSamples) + 128);

    for (std::size_t pass = 1; pass < late.size(); ++pass) {
        INFO("pass " << pass);
        REQUIRE(late[pass] == 2 * kBeatSamples);
    }
}

TEST_CASE("A loop switched off is not a pass boundary", "[engine][io][record][2461]") {
    // The loop end is close enough that a take reading ahead would have named a
    // boundary for it. No wrap comes: the loop goes away first, and continuous
    // audio must not turn into takes of a loop that never came round.
    Rig rig(emptyDirectory("loop_switched_off"), floatTake({0, 1}, -128));
    rig.loop(0.0, 2.0);
    rig.play();
    rig.run((2 * kBeatSamples) - 64);
    rig.unloop();
    rig.run((2 * kBeatSamples) + 64);

    const auto take = rig.recorder().finish();
    CHECK(take.clip.takes.empty());
    CHECK(readBack(take.file).getNumSamples() == (4 * kBeatSamples) + 128);
    CHECK(take.startBeat == Catch::Approx(0.0));
    CHECK(take.lengthBeats == Catch::Approx(4.032));
}

TEST_CASE("A take's length is measured where its audio sits, not where it arrived",
          "[engine][io][record][2461]") {
    // A step from 120 to 240 bpm at beat 1. The head correction moves the
    // recorded stretch a latency earlier, and the beats it covers there are not
    // the beats the arriving blocks counted.
    Rig rig(emptyDirectory("tempo_step"), floatTake({0, 1}, 128), 2,
            TempoMap({{0.0, 120.0, 0.0f}, {1.0, 120.0, 0.0f}, {1.0, 240.0, 0.0f}}, {{0.0, 4, 4}}));
    rig.play();
    rig.run(8000);

    const auto take = rig.recorder().finish();
    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 8000 - 128);

    // A beat is 4000 samples until beat 1 and 2000 after it, so what the file
    // holds reaches beat 2.936 and the clip has to say so.
    CHECK(take.startBeat == Catch::Approx(0.0));
    CHECK(take.lengthBeats == Catch::Approx(2.936));
}

TEST_CASE("A pass end the write path could not take is reported", "[engine][io][record][2461]") {
    // A loop this short fills the boundary lane long before the audio queue,
    // so the passes run together in one file. That is not a shorter recording
    // and must not read as a clean one.
    constexpr int kLoopSamples = 64;
    constexpr int kPasses = 300;

    Rig rig(emptyDirectory("boundary_overflow"), floatTake({0, 1}));
    rig.loop(0.0, static_cast<double>(kLoopSamples) / kBeatSamples);
    rig.play();
    rig.run(kPasses * kLoopSamples);

    const auto take = rig.recorder().finish();
    CHECK(take.samplesLost == 0);
    CHECK(take.passesLost > 0);
    CHECK(static_cast<int>(take.clip.takes.size()) < kPasses - 1);
}

TEST_CASE("A stop mid-pass keeps what was recorded up to it", "[engine][io][record][2461]") {
    Rig rig(emptyDirectory("stop_mid_pass"), floatTake({0, 1}));
    rig.play();
    rig.run(1000);
    rig.stop();
    rig.run(256);

    CHECK_FALSE(rig.recorder().rolling());

    const auto take = rig.recorder().finish();
    CHECK(readBack(take.file).getNumSamples() == 1000);
    CHECK(take.lengthBeats == Catch::Approx(1000.0 / kBeatSamples));
}

TEST_CASE("A mono input is recorded mono", "[engine][io][record][2461]") {
    Rig rig(emptyDirectory("mono"), floatTake({1}));
    rig.play();
    rig.run(256);

    const auto stored = readBack(rig.recorder().finish().file);
    REQUIRE(stored.getNumChannels() == 1);
    CHECK(stored.getSample(0, 0) == material(0, 1));
}

TEST_CASE("An overrun reaches the finished take", "[engine][io][record][2461]") {
    auto settings = floatTake({0, 1});
    settings.stream.capacitySamples = 1024;

    // Nothing drains until the take is closed, so a queue this small is a disk
    // that stopped writing: what it could not hold is lost.
    Rig rig(emptyDirectory("overrun"), std::move(settings));
    rig.play();
    rig.run(4000);

    const auto take = rig.recorder().finish();
    CHECK(take.samplesLost > 0);
    CHECK(readBack(take.file).getNumSamples() == 1024);
}

TEST_CASE("A slot take begins on the sample its launch fired on", "[engine][io][record][2464]") {
    SlotLaunch launch;

    Rig rig(emptyDirectory("slot_launch"), slotTake(launch));
    rig.follows(launch);
    rig.play();

    // Beat one is 4000 samples in and a callback is 64, so the launch fires 32
    // samples inside a block rather than on its edge.
    launch.launch(1.0);
    rig.run(3 * kBeatSamples);

    const auto take = rig.recorder().finish();
    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 2 * kBeatSamples);

    // The arrival at the beat, not the one the block started on.
    CHECK(arrivalAt(stored, 0) == kBeatSamples);
    CHECK(arrivalAt(stored, stored.getNumSamples() - 1) == (3 * kBeatSamples) - 1);

    CHECK(take.startBeat == Catch::Approx(1.0));
    CHECK(take.lengthBeats == Catch::Approx(2.0));
    CHECK(take.clip.takes.empty());
}

TEST_CASE("A slot take ends where its run ended", "[engine][io][record][2464]") {
    // Beat two and a half, which lands 16 samples inside a callback.
    constexpr int kStopArrival = (5 * kBeatSamples) / 2;

    SlotLaunch launch;

    const auto directory = emptyDirectory("slot_stop");
    Rig rig(directory, slotTake(launch));
    rig.follows(launch);
    rig.play();

    launch.launch(1.0);
    rig.run(2 * kBeatSamples);

    launch.stop(2.5);
    rig.run(2 * kBeatSamples);

    CHECK_FALSE(rig.recorder().rolling());
    const auto stopped = rig.recorder().capturedSamples();

    SECTION("the take holds the arrivals between the launch and the stop") {
        const auto stored = readBack(rig.recorder().finish().file);
        REQUIRE(stored.getNumSamples() == kStopArrival - kBeatSamples);
        CHECK(arrivalAt(stored, 0) == kBeatSamples);
        CHECK(arrivalAt(stored, stored.getNumSamples() - 1) == kStopArrival - 1);
    }

    SECTION("a re-launch after it is a second take, not a longer one") {
        launch.launch(4.5);
        rig.run(2 * kBeatSamples);

        CHECK_FALSE(rig.recorder().rolling());
        CHECK(rig.recorder().capturedSamples() == stopped);

        const auto stored = readBack(rig.recorder().finish().file);
        CHECK(stored.getNumSamples() == kStopArrival - kBeatSamples);
        CHECK(arrivalAt(stored, stored.getNumSamples() - 1) == kStopArrival - 1);
        CHECK(directory.getNumberOfChildFiles(juce::File::findFiles) == 1);
    }
}

TEST_CASE("A transport wrap inside a run does not split the slot take",
          "[engine][io][record][2464]") {
    // A run is on monotonic time, so the loop the transport is playing is none
    // of its business: three passes of the loop are one take.
    SlotLaunch launch;

    const auto directory = emptyDirectory("slot_loop_wrap");
    Rig rig(directory, slotTake(launch));
    rig.follows(launch);
    rig.loop(0.0, 2.0);
    rig.play();

    launch.launch(1.0);
    rig.run(3 * 2 * kBeatSamples);

    const auto take = rig.recorder().finish();
    CHECK(take.clip.takes.empty());
    CHECK(directory.getNumberOfChildFiles(juce::File::findFiles) == 1);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 5 * kBeatSamples);

    // One unbroken stretch of the input across the first wrap, which is take
    // sample 4000 and arrival 8000.
    CHECK(arrivalAt(stored, 0) == kBeatSamples);
    CHECK(arrivalAt(stored, kBeatSamples) == 2 * kBeatSamples);
    CHECK(arrivalAt(stored, stored.getNumSamples() - 1) == (6 * kBeatSamples) - 1);

    CHECK(take.startBeat == Catch::Approx(1.0));
    CHECK(take.lengthBeats == Catch::Approx(5.0));
}

TEST_CASE("A slot take is corrected for the input's latency too", "[engine][io][record][2464]") {
    SlotLaunch launch;

    Rig rig(emptyDirectory("slot_latency"), slotTake(launch, 128));
    rig.follows(launch);
    rig.play();

    launch.launch(1.0);
    rig.run(3 * kBeatSamples);

    const auto stored = readBack(rig.recorder().finish().file);
    REQUIRE(stored.getNumSamples() == (2 * kBeatSamples) - 128);

    // Dropped from the head of the run, not from the head of the block it
    // fired in.
    CHECK(arrivalAt(stored, 0) == kBeatSamples + 128);
    CHECK(arrivalAt(stored, stored.getNumSamples() - 1) == (3 * kBeatSamples) - 1);
}

TEST_CASE("A scene's takes all begin on the same sample", "[engine][io][record][2464]") {
    // The launcher cuts one block at one sample for every slot of the scene, so
    // the takes agree about where they start rather than each rounding to its
    // own callback (#2464).
    constexpr int kTracks = 4;

    std::vector<LaunchHandle> handles(kTracks);
    LaunchHandleFeed feed;
    LaunchRequestQueue requests;

    auto table = std::make_shared<LaunchHandleTable>();
    std::map<SlotKey, std::uint64_t> incarnations;

    for (auto track = 0; track < kTracks; ++track) {
        const SlotKey key{static_cast<magda::TrackId>(track + 1), 0};
        table->entries.push_back(LaunchHandleTable::Entry{
            .key = key, .handle = &handles[static_cast<std::size_t>(track)], .incarnation = 1});
        incarnations[key] = 1;
    }

    feed.publish(std::move(table));
    requests.setIncarnations(std::move(incarnations));

    const auto directory = emptyDirectory("slot_scene");

    LiveInputFeed input;
    input.prepare(2, kBlockSize);

    magda::engine::RecordTap taps[kTracks]{
        {magda::engine::RecordMaterial::audio, {}},
        {magda::engine::RecordMaterial::audio, {}},
        {magda::engine::RecordMaterial::audio, {}},
        {magda::engine::RecordMaterial::audio, {}},
    };

    std::vector<std::unique_ptr<TakeRecorder>> takes;
    for (auto track = 0; track < kTracks; ++track) {
        auto settings = floatTake({0, 1});
        settings.directory = directory;
        settings.name = "scene" + juce::String(track);
        settings.slot = SlotRunTarget{.handles = &feed,
                                      .key = SlotKey{static_cast<magda::TrackId>(track + 1), 0},
                                      .incarnation = 1};

        takes.push_back(std::make_unique<TakeRecorder>(
            input, RenderContext{kSampleRate, kBlockSize, 2}, taps[track], std::move(settings)));
    }

    // Every slot in one gesture, which is what makes a scene one event.
    {
        LaunchRequestQueue::Gesture gesture(requests);
        std::vector<SlotKey> slots;
        for (auto track = 0; track < kTracks; ++track)
            slots.push_back(SlotKey{static_cast<magda::TrackId>(track + 1), 0});

        gesture.playScene(slots.front(), slots, 1.0);
    }

    TransportSnapshot transport;
    transport.tempo = TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
    transport.request = {.generation = 1, .playing = true, .locate = true, .positionBeat = 0.0};

    TransportClock clock;
    juce::AudioBuffer<float> block(2, kBlockSize);

    for (std::int64_t arrival = 0; arrival < 3 * kBeatSamples; arrival += kBlockSize) {
        for (auto channel = 0; channel < block.getNumChannels(); ++channel)
            for (auto at = 0; at < kBlockSize; ++at)
                block.setSample(channel, at, material(arrival + at, channel));

        input.beginCallback({juce::dsp::AudioBlock<const float>(block), {}}, kBlockSize);

        for (const auto& segment : clock.advance(transport, kSampleRate, kBlockSize)) {
            input.beginSegment(segment.startSample, segment.block.numSamples);
            advanceLaunchHandles(feed, requests, segment.block);

            for (auto& take : takes)
                take->capture(segment.block, segment.countingIn, transport.loop);
        }

        input.endCallback();
    }

    for (auto& take : takes) {
        const auto recorded = take->finish();
        REQUIRE_FALSE(recorded.empty());

        const auto stored = readBack(recorded.file);
        CHECK(arrivalAt(stored, 0) == kBeatSamples);
        CHECK(recorded.startBeat == Catch::Approx(1.0));
    }
}
