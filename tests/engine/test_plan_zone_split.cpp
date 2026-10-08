#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "plan/RenderPlan.hpp"

// The MidiZoneSplit op: a pad's notes across its layers by their zones (#3007).

using magda::ChainZones;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::DeviceKey;
using magda::engine::EngineDevice;
using magda::engine::EngineMidiSource;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanOp;
using magda::engine::PlanValues;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

constexpr int kBlockSize = 64;

/** @brief Hands over the next block's messages, all at sample zero. */
class QueueSource final : public EngineMidiSource {
  public:
    void render(const BlockInfo&, juce::MidiBuffer& out) override {
        for (const auto& message : next)
            out.addEvent(message, 0);
        next.clear();
    }

    std::vector<juce::MidiMessage> next;
};

/** @brief Records every message that reaches it. */
class MidiCapture final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
        if (block.midiIn != nullptr)
            for (const auto metadata : *block.midiIn)
                seen.push_back(metadata.getMessage());
    }

    std::vector<int> noteOnVelocities() const {
        std::vector<int> velocities;
        for (const auto& message : seen)
            if (message.isNoteOn())
                velocities.push_back(message.getVelocity());
        return velocities;
    }

    int noteOffs() const {
        return static_cast<int>(std::ranges::count_if(
            seen, [](const juce::MidiMessage& message) { return message.isNoteOff(); }));
    }

    std::vector<juce::MidiMessage> seen;
};

/// MidiInput -> MidiZoneSplit -> one Device per layer, the shape a layered pad has.
struct SplitHarness {
    RenderPlan plan;
    PlanExecutor executor;
    PlanBindings bindings;
    QueueSource source;
    std::vector<std::unique_ptr<MidiCapture>> layers;

    explicit SplitHarness(std::vector<ChainZones> zones) {
        PlanOp input;
        input.kind = OpKind::MidiInput;
        input.key.trackId = 1;
        input.key.role = OpRole::LiveMidiInput;
        input.outputs = {SignalKind::Midi};
        plan.ops.push_back(input);

        PlanOp split;
        split.kind = OpKind::MidiZoneSplit;
        split.key.trackId = 1;
        split.key.rackId = -5;
        split.key.chainId = 0;
        split.key.deviceId = 3;
        split.key.role = OpRole::PadLayerSplit;
        split.inputs = {PortRef{0, 0}};
        split.outputs.assign(zones.size(), magda::engine::PortDesc{SignalKind::Midi});
        split.zoneRoutes = zones;
        plan.ops.push_back(split);

        std::vector<PortRef> audio;
        for (std::size_t i = 0; i < zones.size(); ++i) {
            PlanOp device;
            device.kind = OpKind::Device;
            device.key.trackId = 1;
            device.key.rackId = -5;
            device.key.chainId = static_cast<int>(i) + 1;
            device.key.deviceId = 10 + static_cast<int>(i);
            device.key.role = OpRole::DeviceProcess;
            device.inputs = {PortRef{}, PortRef{1, static_cast<int>(i)}, PortRef{}};
            device.outputs = {SignalKind::Audio};
            audio.push_back(PortRef{static_cast<int>(plan.ops.size()), 0});
            plan.ops.push_back(device);

            layers.push_back(std::make_unique<MidiCapture>());
            bindings.devices[DeviceKey{10 + static_cast<int>(i)}] = layers.back().get();
        }

        PlanOp mix;
        mix.kind = OpKind::MixAudio;
        mix.key.trackId = 1;
        mix.key.rackId = -5;
        mix.key.role = OpRole::RackMix;
        mix.inputs = audio;
        mix.outputs = {SignalKind::Audio};
        plan.ops.push_back(mix);

        PlanOp out;
        out.kind = OpKind::Output;
        out.key.trackId = 1;
        out.key.role = OpRole::HardwareOutput;
        out.inputs = {PortRef{static_cast<int>(plan.ops.size()) - 1, 0}};
        plan.ops.push_back(out);

        plan.outputOps = {static_cast<int>(plan.ops.size()) - 1};
        magda::engine::bakeScheduling(plan);
        bindings.midiInputs[1] = &source;

        const auto problems = magda::engine::validatePlan(plan);
        for (const auto& problem : problems)
            UNSCOPED_INFO("validate: " << problem);
        REQUIRE(problems.empty());

        const auto messages =
            executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
        for (const auto& message : messages)
            UNSCOPED_INFO("prepare: " << message);
        REQUIRE(messages.empty());
    }

    void render(std::vector<juce::MidiMessage> messages) {
        source.next = std::move(messages);
        juce::AudioBuffer<float> output(2, kBlockSize);
        output.clear();
        BlockInfo block;
        block.numSamples = kBlockSize;
        executor.process(PlanValues{}, block, output);
    }
};

juce::MidiMessage on(int note, int velocity) {
    return juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity));
}

ChainZones velocityZone(int low, int high) {
    ChainZones zones;
    zones.velocityLow = low;
    zones.velocityHigh = high;
    return zones;
}

}  // namespace

TEST_CASE("A zone split sends each note-on to the layers whose velocity zone takes it",
          "[engine][exec][zonesplit][3007]") {
    SplitHarness harness({velocityZone(1, 63), velocityZone(64, 127)});
    harness.render({on(36, 40), on(38, 100)});

    CHECK(harness.layers[0]->noteOnVelocities() == std::vector<int>{40});
    CHECK(harness.layers[1]->noteOnVelocities() == std::vector<int>{100});
}

TEST_CASE("A note-off follows its note-on rather than its own velocity",
          "[engine][exec][zonesplit][3007]") {
    SplitHarness harness({velocityZone(1, 63), velocityZone(64, 127)});
    harness.render({on(36, 100)});
    // Released with velocity 10, which would land in the soft layer's zone.
    harness.render({juce::MidiMessage::noteOff(1, 36, static_cast<juce::uint8>(10))});

    CHECK(harness.layers[0]->noteOffs() == 0);
    CHECK(harness.layers[1]->noteOffs() == 1);
}

TEST_CASE("A velocity crossfade scales the note-on into a layer",
          "[engine][exec][zonesplit][3007]") {
    auto soft = velocityZone(1, 80);
    soft.velocityFadeHigh = 20;
    SplitHarness harness({soft});
    harness.render({on(36, 80), on(37, 40)});

    // At the top edge the fade has one step of twenty-one left.
    const auto velocities = harness.layers[0]->noteOnVelocities();
    REQUIRE(velocities.size() == 2);
    CHECK(velocities[0] == 4);
    CHECK(velocities[1] == 40);
}

TEST_CASE("Round-robin layers take turns while a plain layer plays every hit",
          "[engine][exec][zonesplit][3007]") {
    ChainZones turn;
    turn.roundRobin = true;
    SplitHarness harness({ChainZones{}, turn, turn});
    harness.render({on(36, 100)});
    harness.render({on(36, 100)});
    harness.render({on(36, 100)});

    CHECK(harness.layers[0]->noteOnVelocities().size() == 3);
    CHECK(harness.layers[1]->noteOnVelocities().size() == 2);
    CHECK(harness.layers[2]->noteOnVelocities().size() == 1);
}

TEST_CASE("A key zone gates by note", "[engine][exec][zonesplit][3007]") {
    ChainZones low;
    low.keyHigh = 59;
    ChainZones high;
    high.keyLow = 60;
    SplitHarness harness({low, high});
    harness.render({on(48, 100), on(72, 100)});

    REQUIRE(harness.layers[0]->seen.size() == 1);
    CHECK(harness.layers[0]->seen[0].getNoteNumber() == 48);
    REQUIRE(harness.layers[1]->seen.size() == 1);
    CHECK(harness.layers[1]->seen[0].getNoteNumber() == 72);
}
