#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "plan/RenderPlan.hpp"

// The MidiNoteGate op (#2200).
//
// A pad answers to a contiguous range of notes and plays them from its own
// root, so a sampler mapped at C0 sounds from whichever pad triggered it. The
// op is what the plan needs before a Drum Grid can be expanded like a rack.

using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::DeviceKey;
using magda::engine::EngineDevice;
using magda::engine::EngineMidiSource;
using magda::engine::OpKind;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

constexpr int kBlockSize = 64;

/// Emits one message per note it is given, all at sample zero.
class NoteSource final : public EngineMidiSource {
  public:
    explicit NoteSource(std::vector<juce::MidiMessage> messages) : messages_(std::move(messages)) {}

    void render(const BlockInfo&, juce::MidiBuffer& out) override {
        for (const auto& message : messages_)
            out.addEvent(message, 0);
    }

  private:
    std::vector<juce::MidiMessage> messages_;
};

/// Records every message that reaches it.
class MidiCapture final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
        if (block.midiIn == nullptr)
            return;
        for (const auto metadata : *block.midiIn)
            seen.push_back(metadata.getMessage());
    }

    std::vector<int> noteOnNumbers() const {
        std::vector<int> notes;
        for (const auto& message : seen)
            if (message.isNoteOn())
                notes.push_back(message.getNoteNumber());
        return notes;
    }

    std::vector<juce::MidiMessage> seen;
};

/// MidiInput -> MidiNoteGate -> Device, which is the shape a pad chain has.
struct GateHarness {
    RenderPlan plan;
    PlanExecutor executor;
    PlanBindings bindings;
    NoteSource source;
    MidiCapture capture;

    GateHarness(std::vector<juce::MidiMessage> messages, int low, int high, int transpose)
        : source(std::move(messages)) {
        magda::engine::PlanOp input;
        input.kind = OpKind::MidiInput;
        input.key.trackId = 1;
        input.key.role = magda::engine::OpRole::LiveMidiInput;
        input.outputs = {SignalKind::Midi};
        plan.ops.push_back(input);

        magda::engine::PlanOp gate;
        gate.kind = OpKind::MidiNoteGate;
        gate.key.trackId = 1;
        gate.key.rackId = 5;
        gate.key.chainId = 2;
        gate.key.role = magda::engine::OpRole::PadNoteGate;
        gate.inputs = {PortRef{0, 0}};
        gate.outputs = {SignalKind::Midi};
        gate.noteGateLow = static_cast<std::uint8_t>(low);
        gate.noteGateHigh = static_cast<std::uint8_t>(high);
        gate.noteGateTranspose = static_cast<std::int8_t>(transpose);
        plan.ops.push_back(gate);

        magda::engine::PlanOp device;
        device.kind = OpKind::Device;
        device.key.trackId = 1;
        device.key.rackId = 5;
        device.key.chainId = 2;
        device.key.deviceId = 9;
        device.key.role = magda::engine::OpRole::DeviceProcess;
        device.inputs = {PortRef{}, PortRef{1, 0}, PortRef{}};
        device.outputs = {SignalKind::Audio};
        plan.ops.push_back(device);

        magda::engine::PlanOp out;
        out.kind = OpKind::Output;
        out.key.trackId = 1;
        out.key.role = magda::engine::OpRole::HardwareOutput;
        out.inputs = {PortRef{2, 0}};
        plan.ops.push_back(out);

        plan.outputOps = {3};
        magda::engine::bakeScheduling(plan);

        bindings.midiInputs[1] = &source;
        bindings.devices[DeviceKey{9}] = &capture;
    }

    void render() {
        const auto messages =
            executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
        for (const auto& message : messages)
            UNSCOPED_INFO("prepare: " << message);
        REQUIRE(messages.empty());
        REQUIRE(executor.isPrepared());

        juce::AudioBuffer<float> output(2, kBlockSize);
        output.clear();
        BlockInfo block;
        block.numSamples = kBlockSize;
        executor.process(PlanValues{}, block, output);
    }
};

juce::MidiMessage noteOn(int note) {
    return juce::MidiMessage::noteOn(1, note, 1.0f);
}

/// Records the input bound the executor told it, and renders nothing.
class BoundProbe final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
    }

    void setMidiInputBoundBytes(int bytes) override {
        bound = bytes;
    }

    int bound = -1;
};

}  // namespace

TEST_CASE("A note gate passes only the notes in its range", "[engine][exec][notegate]") {
    GateHarness harness({noteOn(35), noteOn(36), noteOn(38), noteOn(39), noteOn(40)}, 36, 39, 0);
    harness.render();

    CHECK(harness.capture.noteOnNumbers() == std::vector<int>{36, 38, 39});
}

TEST_CASE("A note gate transposes what it passes onto its root", "[engine][exec][notegate]") {
    // A pad taking 38..40 whose sampler is mapped at 60: the range is moved so
    // the bottom of it plays the root.
    GateHarness harness({noteOn(38), noteOn(39), noteOn(40)}, 38, 40, 60 - 38);
    harness.render();

    CHECK(harness.capture.noteOnNumbers() == std::vector<int>{60, 61, 62});
}

TEST_CASE("A note gate with no transposition leaves pitch alone", "[engine][exec][notegate]") {
    GateHarness harness({noteOn(36), noteOn(37)}, 0, 127, 0);
    harness.render();

    CHECK(harness.capture.noteOnNumbers() == std::vector<int>{36, 37});
}

TEST_CASE("A note gate clamps a transposition past the end of the keyboard",
          "[engine][exec][notegate]") {
    // A pad configured to play higher than MIDI goes. The nearest note that
    // exists is what it asked for; dropping it would leave a pad that triggers
    // nothing and looks broken.
    GateHarness harness({noteOn(120), noteOn(127)}, 120, 127, 20);
    harness.render();

    CHECK(harness.capture.noteOnNumbers() == std::vector<int>{127, 127});
}

TEST_CASE("A note gate passes note-offs so a gated note can end", "[engine][exec][notegate]") {
    GateHarness harness({noteOn(38), juce::MidiMessage::noteOff(1, 38)}, 38, 38, 2);
    harness.render();

    REQUIRE(harness.capture.seen.size() == 2);
    CHECK(harness.capture.seen[0].isNoteOn());
    CHECK(harness.capture.seen[0].getNoteNumber() == 40);
    CHECK(harness.capture.seen[1].isNoteOff());

    // The off has to follow the on, or the note it ends is a different one and
    // the pad hangs.
    CHECK(harness.capture.seen[1].getNoteNumber() == 40);
}

TEST_CASE("A note gate lets non-note messages through untouched", "[engine][exec][notegate]") {
    // A sustain pedal or a pitch bend carries no pitch to gate on and is the
    // chain's business as much as any pad's.
    GateHarness harness({juce::MidiMessage::controllerEvent(1, 64, 127),
                         juce::MidiMessage::pitchWheel(1, 4096), noteOn(20)},
                        36, 39, 0);
    harness.render();

    REQUIRE(harness.capture.seen.size() == 2);
    CHECK(harness.capture.seen[0].isController());
    CHECK(harness.capture.seen[1].isPitchWheel());
}

TEST_CASE("A note gate is a MidiNoteGate in a plan dump", "[engine][exec][notegate]") {
    CHECK(std::string(magda::engine::toString(OpKind::MidiNoteGate)) == "MidiNoteGate");
    CHECK(std::string(magda::engine::toString(magda::engine::OpRole::PadNoteGate)) ==
          "padNoteGate");
    CHECK(magda::engine::arityOf(OpKind::MidiNoteGate) == 1);
}

TEST_CASE("A note gate carries its input's MIDI bound to what it feeds",
          "[engine][exec][notegate][2341]") {
    // A gate is a pure conduit: everything it emits arrived, so its port has to
    // be reserved for what reached it. The forward pass recomputed the bound
    // for a merge and left every other op at one producer's budget, which made
    // a gate the place a two-source stream stopped being one -- the pad's
    // sampler was told 4096 for an input carrying 8192, and sized whatever it
    // buffers from a figure half the truth.
    RenderPlan plan;

    for (int source = 0; source < 2; ++source) {
        magda::engine::PlanOp input;
        input.kind = OpKind::MidiInput;
        input.key.trackId = 1;
        input.key.role = magda::engine::OpRole::LiveMidiInput;
        input.key.index = source;
        input.outputs = {SignalKind::Midi};
        plan.ops.push_back(input);
    }

    magda::engine::PlanOp merge;
    merge.kind = OpKind::MergeMidi;
    merge.key.trackId = 1;
    merge.key.role = magda::engine::OpRole::TrackMidiInput;
    merge.inputs = {PortRef{0, 0}, PortRef{1, 0}};
    merge.outputs = {SignalKind::Midi};
    plan.ops.push_back(merge);

    magda::engine::PlanOp gate;
    gate.kind = OpKind::MidiNoteGate;
    gate.key.trackId = 1;
    gate.key.rackId = 5;
    gate.key.chainId = 2;
    gate.key.role = magda::engine::OpRole::PadNoteGate;
    gate.inputs = {PortRef{2, 0}};
    gate.outputs = {SignalKind::Midi};
    gate.noteGateLow = 36;
    gate.noteGateHigh = 39;
    plan.ops.push_back(gate);

    magda::engine::PlanOp device;
    device.kind = OpKind::Device;
    device.key.trackId = 1;
    device.key.rackId = 5;
    device.key.chainId = 2;
    device.key.deviceId = 9;
    device.key.role = magda::engine::OpRole::DeviceProcess;
    device.inputs = {PortRef{}, PortRef{3, 0}, PortRef{}};
    device.outputs = {SignalKind::Audio};
    plan.ops.push_back(device);

    magda::engine::PlanOp out;
    out.kind = OpKind::Output;
    out.key.trackId = 1;
    out.key.role = magda::engine::OpRole::HardwareOutput;
    out.inputs = {PortRef{4, 0}};
    plan.ops.push_back(out);

    plan.outputOps = {5};
    magda::engine::bakeScheduling(plan);

    BoundProbe probe;
    PlanBindings bindings;
    NoteSource source({});
    bindings.midiInputs[1] = &source;
    bindings.devices[DeviceKey{9}] = &probe;

    PlanExecutor executor;
    const auto messages = executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
    for (const auto& message : messages)
        UNSCOPED_INFO("prepare: " << message);
    REQUIRE(executor.isPrepared());

    CHECK(probe.bound == 2 * magda::engine::kMaxMidiBytesPerPort);
}
