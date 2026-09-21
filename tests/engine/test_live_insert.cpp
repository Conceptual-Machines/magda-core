#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/LiveInput.hpp"
#include "io/LiveInsert.hpp"
#include "io/LiveOutput.hpp"
#include "magda/daw/engine/host/HardwareMidiOutput.hpp"
#include "plan/PlanCompiler.hpp"

/// @file A hardware insert against the audio device, and what binds it (#2279).

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceKey;
using magda::engine::EngineInsert;
using magda::engine::LiveInputBlock;
using magda::engine::LiveInputFeed;
using magda::engine::LiveInsert;
using magda::engine::LiveInsertRoute;
using magda::engine::LiveMidiOutput;
using magda::engine::LiveOutputFeed;
using magda::engine::RenderContext;

namespace {

constexpr int kBlock = 16;

struct Sent {
    int sample = 0;
    std::vector<std::uint8_t> bytes;
};

class RecordingMidiOutput final : public LiveMidiOutput {
  public:
    void send(int sample, const std::uint8_t* data, int size) override {
        sent.push_back({sample, std::vector<std::uint8_t>(data, data + size)});
    }
    void sendNow(const juce::MidiMessage& message) override {
        now.push_back(message);
    }

    std::vector<Sent> sent;
    std::vector<juce::MidiMessage> now;
};

/// A callback's worth of device: four outputs, four inputs holding their own index + 1.
struct Device {
    Device() : output(4, kBlock * 2), input(4, kBlock * 2) {
        output.clear();
        for (auto channel = 0; channel < input.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(input.getWritePointer(channel),
                                              static_cast<float>(channel + 1),
                                              input.getNumSamples());
        inputs.prepare(4, kBlock * 2);
    }

    /// The callback's second block, so offsets from its start are told apart from the block's.
    void begin() {
        const juce::dsp::AudioBlock<const float> audio(input);
        inputs.beginCallback(LiveInputBlock{audio, {}}, kBlock * 2);
        inputs.beginSegment(kBlock, kBlock);
        outputs.beginCallback(output);
        outputs.beginSegment(kBlock, kBlock);
    }

    void end() {
        inputs.endCallback();
        outputs.endCallback();
    }

    juce::AudioBuffer<float> output;
    juce::AudioBuffer<float> input;
    LiveInputFeed inputs;
    LiveOutputFeed outputs;
};

BlockInfo playingBlock() {
    BlockInfo block;
    block.numSamples = kBlock;
    block.playing = true;
    block.continuous = true;
    return block;
}

}  // namespace

TEST_CASE("A live insert's send is added to the channels it names, in its block",
          "[engine][io][insert]") {
    Device device;
    LiveInsert insert(device.inputs, device.outputs, {.sendChannels = {2, 3}});

    juce::AudioBuffer<float> send(2, kBlock);
    send.clear();
    juce::FloatVectorOperations::fill(send.getWritePointer(0), 0.5f, kBlock);
    juce::FloatVectorOperations::fill(send.getWritePointer(1), -0.25f, kBlock);

    device.begin();
    insert.send(playingBlock(), juce::dsp::AudioBlock<const float>(send), {});
    device.end();

    CHECK(device.output.getSample(2, kBlock) == Catch::Approx(0.5f));
    CHECK(device.output.getSample(3, kBlock) == Catch::Approx(-0.25f));
    CHECK(device.output.getSample(2, 0) == 0.0f);
    CHECK(device.output.getSample(0, kBlock) == 0.0f);
    CHECK(insert.missingSendBlocks() == 0);
}

TEST_CASE("A mono send carries the left channel, and a missing one is counted",
          "[engine][io][insert]") {
    Device device;
    LiveInsert mono(device.inputs, device.outputs, {.sendChannels = {1}});
    LiveInsert missing(device.inputs, device.outputs, {.sendChannels = {9}});

    juce::AudioBuffer<float> send(2, kBlock);
    juce::FloatVectorOperations::fill(send.getWritePointer(0), 0.5f, kBlock);
    juce::FloatVectorOperations::fill(send.getWritePointer(1), 0.75f, kBlock);

    device.begin();
    mono.send(playingBlock(), juce::dsp::AudioBlock<const float>(send), {});
    missing.send(playingBlock(), juce::dsp::AudioBlock<const float>(send), {});
    device.end();

    CHECK(device.output.getSample(1, kBlock) == Catch::Approx(0.5f));
    CHECK(missing.missingSendBlocks() == 1);
}

TEST_CASE("A live insert returns the input channels it names", "[engine][io][insert]") {
    Device device;
    LiveInsert stereo(device.inputs, device.outputs, {.returnChannels = {2, 3}});
    LiveInsert mono(device.inputs, device.outputs, {.returnChannels = {1}});

    juce::AudioBuffer<float> back(2, kBlock);
    juce::MidiBuffer midi;

    device.begin();
    stereo.receive(playingBlock(), juce::dsp::AudioBlock<float>(back), midi);
    CHECK(back.getSample(0, 0) == 3.0f);
    CHECK(back.getSample(1, 0) == 4.0f);

    mono.receive(playingBlock(), juce::dsp::AudioBlock<float>(back), midi);
    CHECK(back.getSample(0, 0) == 2.0f);
    CHECK(back.getSample(1, 0) == 2.0f);
    device.end();
}

TEST_CASE("A MIDI send is timed from the callback and releases what it holds",
          "[engine][io][insert]") {
    Device device;
    RecordingMidiOutput port;

    {
        LiveInsert insert(device.inputs, device.outputs, {.midi = &port});

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 3);
        midi.addEvent(juce::MidiMessage::noteOn(2, 64, 1.0f), 5);
        midi.addEvent(juce::MidiMessage::noteOff(2, 64), 7);

        device.begin();
        insert.send(playingBlock(), {}, midi);

        // From the start of the process() call, which is what a port times against.
        REQUIRE(port.sent.size() == 3);
        CHECK(port.sent[0].sample == kBlock + 3);
        CHECK(port.sent[0].bytes == std::vector<std::uint8_t>{0x90, 60, 127});

        // The note still held is ended by a release, the one already ended is not.
        insert.releaseNotes(playingBlock());
        device.end();

        REQUIRE(port.sent.size() == 4);
        CHECK(port.sent[3].bytes == std::vector<std::uint8_t>{0x80, 60, 0});

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);
        device.begin();
        insert.send(playingBlock(), {}, midi);
        device.end();
    }

    // Gone with a note down: it is ended at once, off the audio thread.
    REQUIRE(port.now.size() == 1);
    CHECK(port.now[0].isNoteOff());
    CHECK(port.now[0].getNoteNumber() == 67);
}

TEST_CASE("A hardware MIDI port sends each message when it is due",
          "[engine][host][insert][midi]") {
    magda::daw::engine_host::HardwareMidiClock clock{
        .startMs = 1000.0, .sampleRate = 1000.0, .outputLatencySamples = 10};

    // Heard with the audio beside it: the offset, then the output's latency.
    CHECK(clock.dueMs(5) == Catch::Approx(1015.0));

    std::vector<juce::MidiMessage> delivered;
    magda::daw::engine_host::HardwareMidiPort port(
        nullptr, clock, [&](const juce::MidiMessage& message) { delivered.push_back(message); });

    const std::uint8_t on[] = {0x90, 60, 100};
    const std::uint8_t off[] = {0x80, 60, 0};
    port.send(5, on, 3);
    port.send(20, off, 3);

    const std::uint8_t sysex[] = {0xf0, 1, 2, 0xf7};
    port.send(0, sysex, 4);
    CHECK(port.dropped() == 1);

    port.dispatchDue(1014.0);
    CHECK(delivered.empty());

    port.dispatchDue(1015.0);
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].isNoteOn());

    port.dispatchDue(2000.0);
    REQUIRE(delivered.size() == 2);
    CHECK(delivered[1].isNoteOff());
}

namespace {

class CountingInsert final : public EngineInsert {
  public:
    void send(const BlockInfo&, juce::dsp::AudioBlock<const float>,
              const juce::MidiBuffer&) override {}
    void receive(const BlockInfo&, juce::dsp::AudioBlock<float> audio, juce::MidiBuffer&) override {
        audio.clear();
    }
};

class InsertFactory final : public engine::RuntimeStateFactory {
  public:
    std::unique_ptr<EngineInsert> createInsert(DeviceKey key) override {
        asked.push_back(key);
        return std::make_unique<CountingInsert>();
    }
    std::set<DeviceKey> devicesToRebuild() override {
        return std::exchange(rebuild, {});
    }

    std::vector<DeviceKey> asked;
    std::set<DeviceKey> rebuild;
};

TrackInfo trackWithInsert() {
    TrackInfo track;
    track.id = 1;
    track.type = TrackType::Media;
    track.audioOutputDevice = "master";

    DeviceInfo insert;
    insert.id = 7;
    insert.deviceType = DeviceType::Effect;
    insert.insert.sendType = InsertConfig::Endpoint::Audio;
    insert.insert.returnType = InsertConfig::Endpoint::Audio;
    insert.insert.sendDevice = "Output 3 + 4";
    insert.insert.returnDevice = "Input 3";
    track.chain.fxChainElements.push_back(makeDeviceElement(insert));
    return track;
}

TrackInfo master() {
    TrackInfo track;
    track.id = MASTER_TRACK_ID;
    track.type = TrackType::Master;
    return track;
}

}  // namespace

TEST_CASE("The store asks for an insert once for both halves, and keeps it",
          "[engine][exec][insert]") {
    InsertFactory factory;
    engine::RuntimeStateStore store(factory);
    const RenderContext context{44100.0, 64, 2};

    std::vector<TrackInfo> tracks{trackWithInsert()};
    const auto plan = engine::compileRenderPlan(tracks, master());
    const DeviceKey key{ChainSegment::Fx, 7};

    auto bindings = store.realise(plan, context);
    REQUIRE(factory.asked.size() == 1);
    CHECK(factory.asked[0] == key);
    REQUIRE(bindings.inserts.contains(key));
    const auto* first = bindings.inserts[key];

    // A republish keeps it; a rebuild replaces it.
    bindings = store.realise(plan, context);
    CHECK(factory.asked.size() == 1);
    CHECK(bindings.inserts[key] == first);

    factory.rebuild = {key};
    bindings = store.realise(plan, context);
    CHECK(factory.asked.size() == 2);

    // Deleted from the model and out of the live plan: released.
    const auto empty = engine::compileRenderPlan({}, master());
    std::vector<TrackInfo> none;
    const auto before = store.size();
    CHECK(store.releaseDeleted(empty, engine::collectRuntimeStateIds(none, master()), nullptr) > 0);
    CHECK(store.size() < before);
}
