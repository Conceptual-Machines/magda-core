#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "io/LiveInput.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_live_input.cpp
 * @brief What an armed or monitoring track hears (#2459).
 *
 * The input ops were already compiled and already had bindings; what is new is
 * everything behind them. So these cases are about what a live source reads
 * and when: the callback's own samples, its own piece of a split callback, and
 * nothing at all outside a callback.
 */

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::EngineAudioSource;
using magda::engine::EngineSession;
using magda::engine::kAnyLiveMidiSource;
using magda::engine::kMaxMidiBytesPerPort;
using magda::engine::LiveAudioInput;
using magda::engine::LiveInputFeed;
using magda::engine::LiveMidiInput;
using magda::engine::LiveMidiStream;
using magda::engine::OpKind;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::RuntimeStateFactory;

namespace {

constexpr int kBlockSize = 64;

TrackInfo makeMaster() {
    TrackInfo master;
    master.id = MASTER_TRACK_ID;
    master.type = TrackType::Master;
    master.name = "Master";
    return master;
}

/// A track pointed at hardware, which is what makes its input route External.
TrackInfo monitoringTrack(InputMonitorMode monitor, bool armed) {
    TrackInfo track;
    track.id = 1;
    track.type = TrackType::Media;
    track.name = "Track 1";
    track.audioOutputDevice = "master";
    track.audioInputDevice = "In 1 + 2";
    track.midiInputDevice = "all";
    track.inputMonitor = monitor;
    track.recordArmed = armed;
    return track;
}

int countKind(const RenderPlan& plan, OpKind kind) {
    int found = 0;
    for (const auto& op : plan.ops)
        if (op.kind == kind)
            ++found;
    return found;
}

std::shared_ptr<const RenderPlan> compile(const std::vector<TrackInfo>& tracks) {
    return std::make_shared<const RenderPlan>(
        magda::engine::compileRenderPlan(tracks, makeMaster()));
}

int inputOpsFor(InputMonitorMode monitor, bool armed, OpKind kind) {
    const std::vector<TrackInfo> tracks{monitoringTrack(monitor, armed)};
    return countKind(*compile(tracks), kind);
}

/// A block of samples whose value says which sample it is, so a window onto
/// the wrong part of a callback reads as a wrong number rather than a wrong
/// shape.
juce::AudioBuffer<float> rampBuffer(int numChannels, int numSamples) {
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (auto channel = 0; channel < numChannels; ++channel)
        for (auto sample = 0; sample < numSamples; ++sample)
            buffer.setSample(channel, sample,
                             static_cast<float>(sample) + (100.0f * static_cast<float>(channel)));
    return buffer;
}

juce::dsp::AudioBlock<const float> blockOf(const juce::AudioBuffer<float>& buffer) {
    return juce::dsp::AudioBlock<const float>(buffer);
}

BlockInfo blockInfo(int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.sampleRate = 44100.0;
    return block;
}

/// Renders through a live input and keeps what came out, so a case can assert
/// on the source itself rather than on whatever the chain did to it afterwards.
class TappedAudioInput final : public EngineAudioSource {
  public:
    TappedAudioInput(const LiveInputFeed& feed, std::span<const int> channels)
        : input_(feed, channels) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        input_.render(block, out);

        for (std::size_t sample = 0; sample < out.getNumSamples(); ++sample)
            rendered.push_back(out.getSample(0, static_cast<int>(sample)));

        blockSizes.push_back(static_cast<int>(out.getNumSamples()));
    }

    std::vector<float> rendered;
    std::vector<int> blockSizes;

  private:
    LiveAudioInput input_;
};

/// Binds one live audio input against the session's own feed. The feed is set
/// after the session exists, which is the order a host wires this in: the
/// session owns the feed and the factory is what the session was built with.
class LiveInputFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineAudioSource> createAudioInput(TrackId) override {
        static constexpr std::array<int, 2> stereo{0, 1};
        REQUIRE(feed != nullptr);
        auto source = std::make_unique<TappedAudioInput>(*feed, stereo);
        lastAudioInput = source.get();
        return source;
    }

    LiveInputFeed* feed = nullptr;
    TappedAudioInput* lastAudioInput = nullptr;
};

RenderContext context() {
    return RenderContext{44100.0, kBlockSize, 2};
}

EngineSession::Result publish(EngineSession& session, const std::shared_ptr<const RenderPlan>& plan,
                              const std::vector<TrackInfo>& tracks) {
    PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, makeMaster(), values);
    return session.publish(plan, context(),
                           magda::engine::collectRuntimeStateIds(tracks, makeMaster()),
                           std::move(values));
}

magda::engine::TransportSnapshot rolling(double fromBeat) {
    magda::engine::TransportSnapshot transport;
    transport.request.generation = 1;
    transport.request.playing = true;
    transport.request.positionBeat = fromBeat;
    return transport;
}

}  // namespace

TEST_CASE("An input op is compiled for a track that is armed or monitoring in",
          "[engine][live-input]") {
    // monitorsInput(): armed, or monitoring set to In. Auto lights the activity
    // indicator (receivesLiveMidiInput) but is not audible until the track is
    // armed, which is what ships and what the compiler gates on.
    CHECK(inputOpsFor(InputMonitorMode::Off, false, OpKind::AudioInput) == 0);
    CHECK(inputOpsFor(InputMonitorMode::Auto, false, OpKind::AudioInput) == 0);
    CHECK(inputOpsFor(InputMonitorMode::In, false, OpKind::AudioInput) == 1);
    CHECK(inputOpsFor(InputMonitorMode::Off, true, OpKind::AudioInput) == 1);
    CHECK(inputOpsFor(InputMonitorMode::Auto, true, OpKind::AudioInput) == 1);

    CHECK(inputOpsFor(InputMonitorMode::Off, false, OpKind::MidiInput) == 0);
    CHECK(inputOpsFor(InputMonitorMode::Auto, false, OpKind::MidiInput) == 0);
    CHECK(inputOpsFor(InputMonitorMode::In, false, OpKind::MidiInput) == 1);
    CHECK(inputOpsFor(InputMonitorMode::Off, true, OpKind::MidiInput) == 1);
}

TEST_CASE("A live audio input reads the callback's own samples", "[engine][live-input]") {
    const auto captured = rampBuffer(2, kBlockSize);
    LiveInputFeed feed;
    feed.beginCallback({blockOf(captured), {}}, kBlockSize);
    feed.beginSegment(0, kBlockSize);

    juce::AudioBuffer<float> out(2, kBlockSize);
    out.clear();

    SECTION("channel for channel, where the map names both") {
        static constexpr std::array<int, 2> stereo{0, 1};
        LiveAudioInput input(feed, stereo);
        input.render(blockInfo(kBlockSize), juce::dsp::AudioBlock<float>(out));

        for (auto sample = 0; sample < kBlockSize; ++sample) {
            CHECK(out.getSample(0, sample) == captured.getSample(0, sample));
            CHECK(out.getSample(1, sample) == captured.getSample(1, sample));
        }
        CHECK(input.missingChannelBlocks() == 0);
    }

    SECTION("a mono input fills both ears rather than the left one") {
        static constexpr std::array<int, 1> mono{1};
        LiveAudioInput input(feed, mono);
        input.render(blockInfo(kBlockSize), juce::dsp::AudioBlock<float>(out));

        for (auto sample = 0; sample < kBlockSize; ++sample) {
            CHECK(out.getSample(0, sample) == captured.getSample(1, sample));
            CHECK(out.getSample(1, sample) == captured.getSample(1, sample));
        }
        CHECK(input.missingChannelBlocks() == 0);
    }

    SECTION("a channel the device does not have is silence, and is counted") {
        static constexpr std::array<int, 2> beyond{0, 7};
        LiveAudioInput input(feed, beyond);
        input.render(blockInfo(kBlockSize), juce::dsp::AudioBlock<float>(out));

        CHECK(out.getSample(0, 3) == captured.getSample(0, 3));
        CHECK(out.getSample(1, 3) == 0.0f);
        CHECK(input.missingChannelBlocks() == 1);
    }

    SECTION("and a block of a split callback reads its own part of it") {
        static constexpr std::array<int, 2> stereo{0, 1};
        LiveAudioInput input(feed, stereo);

        const auto half = kBlockSize / 2;
        feed.beginSegment(half, half);

        juce::AudioBuffer<float> second(2, half);
        input.render(blockInfo(half), juce::dsp::AudioBlock<float>(second));

        for (auto sample = 0; sample < half; ++sample)
            CHECK(second.getSample(0, sample) == captured.getSample(0, half + sample));
    }
}

TEST_CASE("A live audio input renders silence with no callback around it", "[engine][live-input]") {
    const auto captured = rampBuffer(2, kBlockSize);
    LiveInputFeed feed;
    static constexpr std::array<int, 2> stereo{0, 1};
    LiveAudioInput input(feed, stereo);

    juce::AudioBuffer<float> out(2, kBlockSize);

    SECTION("before the first one") {
        for (auto sample = 0; sample < kBlockSize; ++sample)
            out.setSample(0, sample, 1.0f);

        input.render(blockInfo(kBlockSize), juce::dsp::AudioBlock<float>(out));
        CHECK(out.getMagnitude(0, kBlockSize) == 0.0f);
    }

    SECTION("and after the last one ends") {
        feed.beginCallback({blockOf(captured), {}}, kBlockSize);
        feed.beginSegment(0, kBlockSize);
        feed.endCallback();

        input.render(blockInfo(kBlockSize), juce::dsp::AudioBlock<float>(out));
        CHECK(out.getMagnitude(0, kBlockSize) == 0.0f);
    }
}

TEST_CASE("A live MIDI input keeps the offsets the host stamped", "[engine][live-input]") {
    juce::MidiBuffer keyboard;
    keyboard.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    keyboard.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 17);
    keyboard.addEvent(juce::MidiMessage::noteOff(1, 60), 63);

    juce::MidiBuffer pads;
    pads.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 20);

    const std::array<LiveMidiStream, 2> streams{LiveMidiStream{1, &keyboard},
                                                LiveMidiStream{2, &pads}};

    LiveInputFeed feed;
    feed.beginCallback({{}, streams}, kBlockSize);
    feed.beginSegment(0, kBlockSize);

    juce::MidiBuffer out;

    SECTION("from the device the track names") {
        LiveMidiInput input(feed, 1);
        input.render(blockInfo(kBlockSize), out);

        std::vector<int> positions;
        std::vector<int> notes;
        for (const auto metadata : out) {
            positions.push_back(metadata.samplePosition);
            notes.push_back(metadata.getMessage().getNoteNumber());
        }

        const std::vector<int> expectedPositions{0, 17, 63};
        const std::vector<int> expectedNotes{60, 64, 60};
        CHECK(positions == expectedPositions);
        CHECK(notes == expectedNotes);
        CHECK(input.droppedEvents() == 0);
    }

    SECTION("or from every device at once") {
        LiveMidiInput input(feed, kAnyLiveMidiSource);
        input.render(blockInfo(kBlockSize), out);

        CHECK(out.getNumEvents() == 4);
    }

    SECTION("and a block of a split callback takes only its own, re-offset") {
        LiveMidiInput input(feed, 1);
        feed.beginSegment(16, 16);
        input.render(blockInfo(16), out);

        REQUIRE(out.getNumEvents() == 1);
        for (const auto metadata : out)
            CHECK(metadata.samplePosition == 1);
    }
}

TEST_CASE("A live MIDI burst past the port's budget is dropped and counted",
          "[engine][live-input]") {
    juce::MidiBuffer flood;
    for (auto event = 0; event < 1000; ++event)
        flood.addEvent(juce::MidiMessage::controllerEvent(1, 74, event % 128), event % kBlockSize);

    const std::array<LiveMidiStream, 1> streams{LiveMidiStream{1, &flood}};

    LiveInputFeed feed;
    feed.beginCallback({{}, streams}, kBlockSize);
    feed.beginSegment(0, kBlockSize);

    LiveMidiInput input(feed, 1);
    juce::MidiBuffer out;
    input.render(blockInfo(kBlockSize), out);

    CHECK(out.data.size() <= kMaxMidiBytesPerPort);
    CHECK(input.droppedEvents() > 0);
    CHECK(out.getNumEvents() + static_cast<int>(input.droppedEvents()) == 1000);
}

TEST_CASE("An input declares its latency and the plan is not compensated for it",
          "[engine][live-input]") {
    LiveInputFeed feed;
    static constexpr std::array<int, 2> stereo{0, 1};
    const LiveAudioInput input(feed, stereo, 512);
    const LiveMidiInput midi(feed, kAnyLiveMidiSource, 512);

    CHECK(input.latencySamples() == 512);
    CHECK(midi.latencySamples() == 512);

    // What arrives has already happened, so the compensation pass is never told
    // about it: delaying the rest of the graph to match would put playback and
    // the click behind the live signal. The number places a recorded take
    // (#2461) and does nothing to the plan.
    const std::vector<TrackInfo> tracks{monitoringTrack(InputMonitorMode::In, false)};
    const auto plan = compile(tracks);
    const auto layout = magda::engine::resolveLayout(*plan, std::vector<int>(plan->ops.size(), 0));

    CHECK(layout.latency.outputLatency == 0);
    for (const auto delay : layout.latency.delaySamples)
        CHECK(delay == 0);
}

TEST_CASE("A monitoring track hears its live input through the plan", "[engine][live-input]") {
    LiveInputFactory factory;
    EngineSession session(factory);
    factory.feed = &session.liveInputs();

    const std::vector<TrackInfo> tracks{monitoringTrack(InputMonitorMode::In, false)};
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);
    REQUIRE(factory.lastAudioInput != nullptr);

    const auto captured = rampBuffer(2, kBlockSize);
    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();

    session.process(kBlockSize, output, {blockOf(captured), {}});

    auto& tapped = *factory.lastAudioInput;
    REQUIRE(tapped.rendered.size() == static_cast<std::size_t>(kBlockSize));

    for (auto sample = 0; sample < kBlockSize; ++sample)
        CHECK(tapped.rendered[static_cast<std::size_t>(sample)] == captured.getSample(0, sample));

    // It reached the master rather than stopping at the op.
    CHECK(output.getMagnitude(0, kBlockSize) > 0.0f);

    // And the feed is closed once the callback is over, so a source reached
    // between callbacks reads nothing rather than the last one's samples.
    juce::AudioBuffer<float> afterwards(2, kBlockSize);
    tapped.render(blockInfo(kBlockSize), juce::dsp::AudioBlock<float>(afterwards));
    CHECK(afterwards.getMagnitude(0, kBlockSize) == 0.0f);
}

TEST_CASE("Each piece of a callback the loop wraps inside reads its own input",
          "[engine][live-input]") {
    LiveInputFactory factory;
    EngineSession session(factory);
    factory.feed = &session.liveInputs();

    const std::vector<TrackInfo> tracks{monitoringTrack(InputMonitorMode::In, false)};
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);
    REQUIRE(factory.lastAudioInput != nullptr);

    // A loop one beat long at 120 bpm is 22050 samples, which a callback of 64
    // lands inside of after 344 of them.
    auto transport = rolling(0.0);
    transport.loop = {true, 0.0, 1.0};
    session.publishTransport(transport);

    juce::AudioBuffer<float> output(2, kBlockSize);
    const auto captured = rampBuffer(2, kBlockSize);

    for (auto block = 0; block < 345; ++block) {
        output.clear();
        session.process(kBlockSize, output, {blockOf(captured), {}});
    }

    auto& tapped = *factory.lastAudioInput;
    REQUIRE(tapped.blockSizes.size() == 346);

    const auto first = tapped.blockSizes[344];
    const auto second = tapped.blockSizes[345];
    CHECK(first + second == kBlockSize);
    CHECK(first > 0);
    CHECK(second > 0);

    // The wrap splits the timeline, not the input: the two pieces read the
    // callback's samples in order, and neither re-reads the first ones.
    const auto tail = tapped.rendered.size() - static_cast<std::size_t>(kBlockSize);
    for (auto sample = 0; sample < kBlockSize; ++sample)
        CHECK(tapped.rendered[tail + static_cast<std::size_t>(sample)] ==
              captured.getSample(0, sample));
}
