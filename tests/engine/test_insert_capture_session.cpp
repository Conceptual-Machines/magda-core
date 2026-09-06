#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "insert/InsertCapturePlayback.hpp"
#include "insert/InsertCaptureSession.hpp"

/**
 * @file test_insert_capture_session.cpp
 * @brief A pass, a capture and a bounce that plays it back (#2279).
 *
 * The claim: what a render gets out of a capture is what the hardware said
 * during the pass, at the same position and with the same round trip, and a
 * capture that cannot say that does not become a playback at all.
 */

using magda::engine::BlockInfo;
using magda::engine::CaptureWindow;
using magda::engine::EngineInsert;
using magda::engine::InsertCapture;
using magda::engine::InsertCapturePlayback;
using magda::engine::InsertCaptureSession;
using magda::engine::RenderContext;

namespace {

constexpr double kRate = 48000.0;
constexpr int kChannels = 2;
constexpr int kBlock = 128;
constexpr int kWindowSamples = 1024;
constexpr int kLatency = 64;

CaptureWindow windowOf(int samples, double rate = kRate) {
    return {0.0, samples / rate};
}

BlockInfo blockAt(std::int64_t startSample, int numSamples, double rate = kRate,
                  bool playing = true) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.sampleRate = rate;
    block.playing = playing;
    block.seconds = {static_cast<double>(startSample) / rate,
                     static_cast<double>(startSample + numSamples) / rate};
    return block;
}

/**
 * @brief What the hardware "says" at one instant.
 *
 * A tone rather than a ramp, so a resampled case has something a wrong curve
 * would visibly damage.
 */
float valueFor(double seconds, int channel, double frequency = 100.0) {
    const auto phase = 2.0 * std::numbers::pi * frequency * seconds;
    return static_cast<float>(std::sin(phase) * (channel == 0 ? 1.0 : 0.5));
}

/// The same instant, named by sample at @p rate.
float valueAt(std::int64_t sample, int channel, double rate = kRate, double frequency = 100.0) {
    return valueFor(static_cast<double>(sample) / rate, channel, frequency);
}

/** @brief One message the stub hands back, at an absolute sample of the pass. */
struct StubMessage {
    std::int64_t sample = 0;
    juce::MidiMessage message;
};

/**
 * @brief The outboard, as far as these tests need one.
 *
 * It answers with a tone derived from where the block is, so what it said at an
 * instant is checkable without recording it a second time.
 */
class HardwareStub final : public EngineInsert {
  public:
    int latencySamples() const override {
        return kLatency;
    }

    void send(const BlockInfo&, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override {
        ++sentBlocks;
        sentSamples += static_cast<int>(audio.getNumSamples());
        sentMidiEvents += midi.getNumEvents();
    }

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override {
        ++receivedBlocks;

        const auto start = std::llround(block.seconds.start * block.sampleRate);

        for (std::size_t channel = 0; channel < audio.getNumChannels(); ++channel)
            for (auto at = 0; at < block.numSamples; ++at)
                audio.setSample(
                    static_cast<int>(channel), at,
                    valueAt(start + at, static_cast<int>(channel), block.sampleRate, frequency));

        for (const auto& pending : returns) {
            const auto offset = pending.sample - start;
            if (offset >= 0 && offset < block.numSamples)
                midi.addEvent(pending.message, static_cast<int>(offset));
        }
    }

    /// What it plays back. Ultrasonic for the case that asks what a rate
    /// reduction does with content the render has no room for.
    double frequency = 100.0;

    std::vector<StubMessage> returns;
    int sentBlocks = 0;
    int sentSamples = 0;
    int sentMidiEvents = 0;
    int receivedBlocks = 0;
};

/** @brief Run @p blockIndices of a pass through @p session, from sample zero. */
void runPass(InsertCaptureSession& session, const std::vector<int>& blockIndices,
             int blockSize = kBlock, int channels = kChannels, double rate = kRate) {
    juce::AudioBuffer<float> buffer(channels, blockSize);
    juce::MidiBuffer midi;

    for (const auto index : blockIndices) {
        buffer.clear();
        midi.clear();

        const auto start = static_cast<std::int64_t>(index) * blockSize;
        const auto block = blockAt(start, blockSize, rate);

        juce::dsp::AudioBlock<float> audio(buffer);
        session.send(block, juce::dsp::AudioBlock<const float>(audio), midi);
        session.receive(block, audio, midi);
    }
}

std::vector<int> everyBlock(int count) {
    std::vector<int> indices;
    for (auto at = 0; at < count; ++at)
        indices.push_back(at);
    return indices;
}

/** @brief A whole pass, taken. */
InsertCapture captureOfWholePass(HardwareStub& hardware) {
    InsertCaptureSession session(hardware, windowOf(kWindowSamples), kRate, kChannels);
    runPass(session, everyBlock(kWindowSamples / kBlock));
    return session.take();
}

RenderContext contextAt(double rate, int channels = kChannels) {
    return RenderContext{rate, 512, channels};
}

/** @brief Everything @p playback returns, in blocks of @p blockSize. */
juce::AudioBuffer<float> replay(InsertCapturePlayback& playback, int blockSize, double rate,
                                int totalSamples, juce::MidiBuffer* collected = nullptr) {
    juce::AudioBuffer<float> whole(kChannels, totalSamples);
    whole.clear();

    juce::AudioBuffer<float> buffer(kChannels, blockSize);
    juce::MidiBuffer midi;

    for (auto at = 0; at < totalSamples; at += blockSize) {
        const auto count = std::min(blockSize, totalSamples - at);

        // Dirtied on purpose: a block comes back filled or cleared, never part
        // of each.
        buffer.clear();
        for (auto channel = 0; channel < kChannels; ++channel)
            for (auto sample = 0; sample < blockSize; ++sample)
                buffer.setSample(channel, sample, -99.0f);

        midi.clear();

        juce::dsp::AudioBlock<float> audio(buffer);
        playback.receive(blockAt(at, count, rate),
                         audio.getSubBlock(0, static_cast<std::size_t>(count)), midi);

        for (auto channel = 0; channel < kChannels; ++channel)
            whole.copyFrom(channel, at, buffer, channel, 0, count);

        if (collected != nullptr)
            for (const auto metadata : midi)
                collected->addEvent(metadata.data, metadata.numBytes, at + metadata.samplePosition);
    }

    return whole;
}

}  // namespace

TEST_CASE("A pass writes what the hardware returned", "[engine][insert][2279]") {
    HardwareStub hardware;
    const auto capture = captureOfWholePass(hardware);

    // Block for block: a session in the way must not change the pass.
    CHECK(hardware.sentBlocks == kWindowSamples / kBlock);
    CHECK(hardware.receivedBlocks == kWindowSamples / kBlock);

    REQUIRE(capture.complete());
    REQUIRE(capture.lengthInSamples() == kWindowSamples);
    CHECK(capture.sampleRate() == kRate);

    // Kept in seconds, so it survives another rate.
    CHECK(capture.roundTripSeconds() == Catch::Approx(kLatency / kRate));

    for (auto channel = 0; channel < kChannels; ++channel)
        for (auto at = 0; at < kWindowSamples; ++at) {
            INFO("channel " << channel << " sample " << at);
            REQUIRE(capture.audio().getSample(channel, at) == valueAt(at, channel));
        }
}

TEST_CASE("A pass that seeked over the window leaves a capture nothing can use",
          "[engine][insert][2279]") {
    HardwareStub hardware;
    InsertCaptureSession session(hardware, windowOf(kWindowSamples), kRate, kChannels);

    // Blocks 4 and 5 never happen, which is what a seek looks like from here.
    runPass(session, {0, 1, 2, 3, 6, 7});

    CHECK(session.missingSamples() == 2 * kBlock);

    const auto capture = session.take();
    CHECK_FALSE(capture.complete());

    // And nothing to bind, so no render writes the hole into a file.
    CHECK(InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate)) ==
          nullptr);
}

TEST_CASE("A pass over the same window twice covers it once", "[engine][insert][2279]") {
    HardwareStub hardware;
    InsertCaptureSession session(hardware, windowOf(kWindowSamples), kRate, kChannels);

    runPass(session, everyBlock(kWindowSamples / kBlock));
    REQUIRE(session.missingSamples() == 0);

    // What is counted is the window covered, not the blocks that went by.
    runPass(session, {0, 1, 2});
    CHECK(session.missingSamples() == 0);
    CHECK(session.take().complete());
}

TEST_CASE("A playback returns the capture where the pass received it", "[engine][insert][2279]") {
    HardwareStub hardware;
    const auto capture = captureOfWholePass(hardware);

    auto playback =
        InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate));
    REQUIRE(playback != nullptr);

    // The live pass's round trip, so the same compensation lines both up.
    CHECK(playback->latencySamples() == kLatency);

    const auto replayed = replay(*playback, 256, kRate, kWindowSamples);

    for (auto channel = 0; channel < kChannels; ++channel)
        for (auto at = 0; at < kWindowSamples; ++at) {
            INFO("channel " << channel << " sample " << at);
            REQUIRE(replayed.getSample(channel, at) == valueAt(at, channel));
        }
}

TEST_CASE("How a bounce cuts its blocks is not in the playback", "[engine][insert][2279]") {
    HardwareStub hardware;
    const auto capture = captureOfWholePass(hardware);

    auto small = InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate));
    auto large = InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate));
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);

    const auto cut = replay(*small, 64, kRate, kWindowSamples);
    const auto whole = replay(*large, 512, kRate, kWindowSamples);

    for (auto channel = 0; channel < kChannels; ++channel)
        for (auto at = 0; at < kWindowSamples; ++at) {
            INFO("channel " << channel << " sample " << at);
            REQUIRE(cut.getSample(channel, at) == whole.getSample(channel, at));
        }
}

TEST_CASE("A capture taken at another rate is resampled, not refused", "[engine][insert][2279]") {
    HardwareStub hardware;
    const auto capture = captureOfWholePass(hardware);

    // Another rate must not need a second trip through the hardware.
    auto playback =
        InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(2.0 * kRate));
    REQUIRE(playback != nullptr);

    // The same duration, which at twice the rate is twice the samples.
    CHECK(playback->latencySamples() == 2 * kLatency);

    const auto replayed = replay(*playback, 256, 2.0 * kRate, 2 * kWindowSamples);

    // A margin because this is interpolated: the claim is that the signal
    // survived, not that a curve reproduced it.
    for (auto at = 8; at < 2 * kWindowSamples - 8; at += 37) {
        const auto seconds = at / (2.0 * kRate);
        INFO("sample " << at);
        REQUIRE(replayed.getSample(0, at) ==
                Catch::Approx(std::sin(2.0 * std::numbers::pi * 100.0 * seconds)).margin(0.01));
    }
}

TEST_CASE("A capture that does not cover the render is refused", "[engine][insert][2279]") {
    HardwareStub hardware;
    InsertCaptureSession session(hardware, windowOf(kBlock), kRate, kChannels);
    runPass(session, {0});

    const auto capture = session.take();
    REQUIRE(capture.complete());

    // It serves what it covers.
    CHECK(InsertCapturePlayback::create(capture, windowOf(kBlock), contextAt(kRate)) != nullptr);

    // And nothing more than that.
    CHECK(InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate)) ==
          nullptr);
}

TEST_CASE("A capture narrower than the render is refused", "[engine][insert][2279]") {
    HardwareStub hardware;
    InsertCaptureSession session(hardware, windowOf(kWindowSamples), kRate, 1);
    runPass(session, everyBlock(kWindowSamples / kBlock));

    const auto capture = session.take();
    REQUIRE(capture.complete());

    // A stereo render reads both channels.
    CHECK(InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate, 2)) ==
          nullptr);
    CHECK(InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate, 1)) !=
          nullptr);
}

TEST_CASE("MIDI comes back where it went in, at the length it was sent", "[engine][insert][2279]") {
    HardwareStub hardware;
    hardware.returns = {
        {10, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100))},
        {kBlock * 2 + 5, juce::MidiMessage::programChange(1, 7)},
        {kBlock * 3, juce::MidiMessage::midiClock()},
    };

    const auto capture = captureOfWholePass(hardware);
    REQUIRE(capture.midi().size() == 3);

    auto playback =
        InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate));
    REQUIRE(playback != nullptr);

    juce::MidiBuffer collected;
    replay(*playback, 256, kRate, kWindowSamples, &collected);

    std::vector<std::pair<int, juce::MidiMessage>> events;
    for (const auto metadata : collected)
        events.emplace_back(metadata.samplePosition, metadata.getMessage());

    REQUIRE(events.size() == 3);

    CHECK(events[0].first == 10);
    CHECK(events[0].second.isNoteOn());
    CHECK(events[0].second.getNoteNumber() == 60);

    // Two bytes, and it has to come back as two: a program change replayed
    // three bytes long is a different message.
    CHECK(events[1].first == kBlock * 2 + 5);
    CHECK(events[1].second.isProgramChange());
    CHECK(events[1].second.getRawDataSize() == 2);

    CHECK(events[2].first == kBlock * 3);
    CHECK(events[2].second.isMidiClock());
    CHECK(events[2].second.getRawDataSize() == 1);
}

TEST_CASE("A block the transport is not moving through returns silence", "[engine][insert][2279]") {
    HardwareStub hardware;
    const auto capture = captureOfWholePass(hardware);

    auto playback =
        InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate));
    REQUIRE(playback != nullptr);

    juce::AudioBuffer<float> buffer(kChannels, kBlock);
    for (auto channel = 0; channel < kChannels; ++channel)
        for (auto at = 0; at < kBlock; ++at)
            buffer.setSample(channel, at, -99.0f);

    juce::MidiBuffer midi;
    juce::dsp::AudioBlock<float> audio(buffer);

    // A bounce's tail runs the graph stopped. Nothing left the machine, so
    // nothing comes back: the outboard's decay is captured only as far as the
    // window went.
    playback->receive(blockAt(0, kBlock, kRate, false), audio, midi);

    for (auto channel = 0; channel < kChannels; ++channel)
        for (auto at = 0; at < kBlock; ++at) {
            INFO("channel " << channel << " sample " << at);
            REQUIRE(buffer.getSample(channel, at) == 0.0f);
        }
}

TEST_CASE("A stretch passed over again keeps the later pass's messages", "[engine][insert][2279]") {
    HardwareStub hardware;
    hardware.returns = {{0, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100))}};

    InsertCaptureSession session(hardware, windowOf(kBlock), kRate, kChannels);
    runPass(session, {0});

    // The same stretch again, saying something else. Its audio replaces what
    // was there, and its messages have to go the same way: a bounce that played
    // both would double every note the pass took back.
    hardware.returns = {{0, juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100))}};
    runPass(session, {0});

    const auto capture = session.take();
    REQUIRE(capture.complete());
    REQUIRE(capture.midi().size() == 1);
    CHECK(capture.midi().front().data1 == 64);
}

TEST_CASE("A pass that loops does not fill up with messages it replaced",
          "[engine][insert][2279]") {
    HardwareStub hardware;
    hardware.returns = {{0, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100))}};

    // Room for four events, and ten passes over the stretch that holds one.
    InsertCaptureSession session(hardware, windowOf(kBlock), kRate, kChannels, 4);
    for (auto pass = 0; pass < 10; ++pass)
        runPass(session, {0});

    const auto capture = session.take();
    CHECK(capture.complete());
    CHECK(capture.midi().size() == 1);
}

TEST_CASE("A return narrower than the capture leaves it incomplete", "[engine][insert][2279]") {
    HardwareStub hardware;
    InsertCaptureSession session(hardware, windowOf(kWindowSamples), kRate, kChannels);

    // Mono blocks into a stereo capture. The width was allocated, not recorded,
    // and the difference would go into the file as a silent channel.
    runPass(session, everyBlock(kWindowSamples / kBlock), kBlock, 1);

    CHECK(session.missingSamples() == kWindowSamples);

    const auto capture = session.take();
    CHECK_FALSE(capture.complete());
    CHECK(InsertCapturePlayback::create(capture, windowOf(kWindowSamples), contextAt(kRate)) ==
          nullptr);
}

/** @brief A capture of @p frequency taken at twice the render's rate. */
InsertCapture captureAt96k(HardwareStub& hardware, double frequency) {
    hardware.frequency = frequency;

    const auto captureRate = 2.0 * kRate;
    InsertCaptureSession session(hardware, windowOf(kWindowSamples, captureRate), captureRate,
                                 kChannels);
    runPass(session, everyBlock(kWindowSamples / kBlock), kBlock, kChannels, captureRate);

    return session.take();
}

TEST_CASE("What the render has room for comes back at its own level and time",
          "[engine][insert][2279]") {
    HardwareStub hardware;

    // 10 kHz, which both rates carry. The filter in front of the rate reduction
    // must leave it where it was: a gain error or a shift here is the same
    // filter being asymmetric, which is what an even tap count makes it.
    const auto capture = captureAt96k(hardware, 10000.0);
    REQUIRE(capture.complete());

    auto playback = InsertCapturePlayback::create(capture, capture.window(), contextAt(kRate));
    REQUIRE(playback != nullptr);

    const auto samples = kWindowSamples / 2;
    const auto replayed = replay(*playback, 256, kRate, samples);

    auto sum = 0.0;
    auto counted = 0;

    for (auto at = 128; at < samples - 128; ++at) {
        const auto value = replayed.getSample(0, at);
        sum += static_cast<double>(value) * value;
        ++counted;

        INFO("sample " << at);
        REQUIRE(value == Catch::Approx(valueAt(at, 0, kRate, 10000.0)).margin(0.02));
    }

    CHECK(std::sqrt(sum / std::max(1, counted)) == Catch::Approx(std::sqrt(0.5)).margin(0.01));
}

TEST_CASE("What the render has no room for does not come back inside it",
          "[engine][insert][2279]") {
    HardwareStub hardware;

    // 30 kHz, which a 96 kHz pass carries and a 48 kHz render cannot: dropped
    // samples alone would fold it to 18 kHz at the level it was sent.
    const auto capture = captureAt96k(hardware, 30000.0);
    REQUIRE(capture.complete());

    auto playback = InsertCapturePlayback::create(capture, capture.window(), contextAt(kRate));
    REQUIRE(playback != nullptr);

    const auto replayed = replay(*playback, 256, kRate, kWindowSamples / 2);

    // Measured away from the ends, where the filter is running into the edge of
    // the capture rather than into more of it.
    auto sum = 0.0;
    auto counted = 0;
    for (auto at = 128; at < (kWindowSamples / 2) - 128; ++at) {
        const auto value = replayed.getSample(0, at);
        sum += static_cast<double>(value) * value;
        ++counted;
    }

    // Far enough down to be the filter working rather than a filter with a
    // hole in it: an asymmetric kernel of the same length leaves it at 0.026.
    const auto rms = std::sqrt(sum / std::max(1, counted));
    INFO("rms " << rms);
    CHECK(rms < 0.005);
}
