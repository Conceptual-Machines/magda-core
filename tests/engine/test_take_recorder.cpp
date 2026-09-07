#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "exec/RenderContext.hpp"
#include "io/LiveInput.hpp"
#include "io/TakeRecorder.hpp"
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
 */

using magda::engine::AudioFileFormat;
using magda::engine::LiveInputBlock;
using magda::engine::LiveInputFeed;
using magda::engine::LoopRange;
using magda::engine::RecordedTake;
using magda::engine::RenderContext;
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
 * @brief A transport, an input and one take, driven a callback at a time.
 */
class Rig {
  public:
    Rig(const juce::File& directory, TakeRecorderSettings settings, int inputChannels = 2)
        : input_(inputChannels, kBlockSize) {
        transport_.tempo = TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
        feed_.prepare(inputChannels, kBlockSize);

        settings.directory = directory;
        recorder_ = std::make_unique<TakeRecorder>(
            feed_, RenderContext{kSampleRate, kBlockSize, inputChannels}, std::move(settings));
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

    /// Feed @p numSamples of callbacks through the take.
    void run(int numSamples) {
        for (auto left = numSamples; left > 0;) {
            const auto callback = std::min(kBlockSize, left);
            deliver(callback);
            left -= callback;
        }
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
            recorder_->capture(segment.block, segment.countingIn, transport_.loop);
        }

        feed_.endCallback();
        arrival_ += numSamples;
    }

    TransportSnapshot transport_;
    TransportClock clock_;
    LiveInputFeed feed_;
    juce::AudioBuffer<float> input_;
    std::unique_ptr<TakeRecorder> recorder_;

    /// Input samples delivered since the rig was made, which is what the
    /// material is numbered by.
    std::int64_t arrival_ = 0;
};

/// The arrival the take's first sample came from, read out of the file itself.
std::int64_t firstArrival(const juce::AudioBuffer<float>& stored) {
    return std::llround(static_cast<double>(stored.getSample(0, 0)) * 1.0e5) - 1;
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
