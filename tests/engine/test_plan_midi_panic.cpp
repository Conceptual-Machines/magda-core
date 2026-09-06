#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "EngineSessionScaffold.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/RenderPlan.hpp"

// The panic flag beside a MIDI port (#2418).
//
// The fork carries all-notes-off on its MIDI container and raises it per plugin
// on a playhead jump or a track it just muted (tracktion_PluginNode.cpp). A
// juce::MidiBuffer has nowhere to put it, so the engine carries it beside the
// port: seeded on the way into a device, read back on the way out, and passed
// along whatever the port feeds.

using magda::TrackId;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::DeviceKey;
using magda::engine::EngineDevice;
using magda::engine::EngineMidiSource;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

constexpr int kBlockSize = 64;

/// Renders nothing, and says whether the block it just rendered was a
/// discontinuity of its own -- what a launched slot is.
class LaunchingSource final : public EngineMidiSource {
  public:
    void render(const BlockInfo& /*block*/, juce::MidiBuffer& /*out*/) override {}

    bool raisedAllNotesOff() const override {
        return launched;
    }

    bool launched = false;
};

/// Renders silence, so a compiled plan's audio slot has a source bound.
class SilentAudio final : public magda::engine::EngineAudioSource {
  public:
    void render(const BlockInfo& /*block*/, juce::dsp::AudioBlock<float> out) override {
        out.clear();
    }
};

/// Records the panic it was handed, block by block, and writes whatever it was
/// told to write on its own port.
class PanicProbe final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
        heard.push_back(block.midiInAllNotesOff);
        if (forwards)
            block.midiOutAllNotesOff = block.midiInAllNotesOff;
        else if (raises)
            block.midiOutAllNotesOff = true;
    }

    bool lastHeard() const {
        REQUIRE(!heard.empty());
        return heard.back();
    }

    std::vector<bool> heard;
    bool forwards = false;
    bool raises = false;
};

/// MidiInput -> Device -> Device -> Output, with the first device's MIDI port
/// feeding the second. The shape a chain of two MIDI devices has, which is what
/// forwarding a panic needs.
struct ChainHarness {
    RenderPlan plan;
    PlanExecutor executor;
    PlanBindings bindings;
    LaunchingSource source;
    PanicProbe first;
    PanicProbe second;
    PlanValues values;
    juce::AudioBuffer<float> output{2, kBlockSize};

    ChainHarness() {
        magda::engine::PlanOp input;
        input.kind = OpKind::MidiInput;
        input.key.trackId = 1;
        input.key.role = OpRole::LiveMidiInput;
        input.outputs = {SignalKind::Midi};
        plan.ops.push_back(input);

        magda::engine::PlanOp emitter;
        emitter.kind = OpKind::Device;
        emitter.key.trackId = 1;
        emitter.key.deviceId = 9;
        emitter.key.role = OpRole::DeviceProcess;
        emitter.inputs = {PortRef{}, PortRef{0, 0}, PortRef{}};
        emitter.outputs = {SignalKind::Audio, SignalKind::Midi};
        plan.ops.push_back(emitter);

        magda::engine::PlanOp reader;
        reader.kind = OpKind::Device;
        reader.key.trackId = 1;
        reader.key.deviceId = 10;
        reader.key.role = OpRole::DeviceProcess;
        reader.inputs = {PortRef{1, 0}, PortRef{1, 1}, PortRef{}};
        reader.outputs = {SignalKind::Audio};
        plan.ops.push_back(reader);

        magda::engine::PlanOp out;
        out.kind = OpKind::Output;
        out.key.trackId = 1;
        out.key.role = OpRole::HardwareOutput;
        out.inputs = {PortRef{2, 0}};
        plan.ops.push_back(out);

        plan.outputOps = {3};
        magda::engine::bakeScheduling(plan);

        bindings.midiInputs[1] = &source;
        bindings.devices[DeviceKey{9}] = &first;
        bindings.devices[DeviceKey{10}] = &second;

        values.planFingerprint = magda::engine::planFingerprint(plan);
        values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);
    }

    void prepare() {
        const auto messages =
            executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
        for (const auto& message : messages)
            UNSCOPED_INFO("prepare: " << message);
        REQUIRE(messages.empty());
    }

    void render(bool continuous = true) {
        output.clear();
        BlockInfo block;
        block.numSamples = kBlockSize;
        block.playing = true;
        block.continuous = continuous;
        executor.process(values, block, output);
    }
};

/// Two MidiInput ops merged into one device, which is what a track with two
/// MIDI sources compiles to.
struct MergeHarness {
    RenderPlan plan;
    PlanExecutor executor;
    PlanBindings bindings;
    LaunchingSource left;
    LaunchingSource right;
    PanicProbe device;
    PlanValues values;
    juce::AudioBuffer<float> output{2, kBlockSize};

    MergeHarness() {
        for (TrackId track : {TrackId{1}, TrackId{2}}) {
            magda::engine::PlanOp input;
            input.kind = OpKind::MidiInput;
            input.key.trackId = track;
            input.key.role = OpRole::LiveMidiInput;
            input.outputs = {SignalKind::Midi};
            plan.ops.push_back(input);
        }

        magda::engine::PlanOp merge;
        merge.kind = OpKind::MergeMidi;
        merge.key.trackId = 1;
        merge.key.role = OpRole::ChainMidiMerge;
        merge.inputs = {PortRef{0, 0}, PortRef{1, 0}};
        merge.outputs = {SignalKind::Midi};
        plan.ops.push_back(merge);

        magda::engine::PlanOp reader;
        reader.kind = OpKind::Device;
        reader.key.trackId = 1;
        reader.key.deviceId = 9;
        reader.key.role = OpRole::DeviceProcess;
        reader.inputs = {PortRef{}, PortRef{2, 0}, PortRef{}};
        reader.outputs = {SignalKind::Audio};
        plan.ops.push_back(reader);

        magda::engine::PlanOp out;
        out.kind = OpKind::Output;
        out.key.trackId = 1;
        out.key.role = OpRole::HardwareOutput;
        out.inputs = {PortRef{3, 0}};
        plan.ops.push_back(out);

        plan.outputOps = {4};
        magda::engine::bakeScheduling(plan);

        bindings.midiInputs[1] = &left;
        bindings.midiInputs[2] = &right;
        bindings.devices[DeviceKey{9}] = &device;

        values.planFingerprint = magda::engine::planFingerprint(plan);
        values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);
    }

    void render() {
        const auto messages =
            executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
        REQUIRE(messages.empty());
        output.clear();
        BlockInfo block;
        block.numSamples = kBlockSize;
        block.playing = true;
        block.continuous = true;
        executor.process(values, block, output);
    }
};

}  // namespace

TEST_CASE("a locate raises the panic on every device it reaches", "[engine][exec][2418]") {
    // BlockInfo::continuous is the engine's playhead jump: false on the first
    // block after a start, a locate or a loop wrap. Both devices hear it,
    // including the one the source does not reach.
    ChainHarness harness;
    harness.prepare();

    harness.render(/*continuous=*/false);
    CHECK(harness.first.lastHeard());
    CHECK(harness.second.lastHeard());

    harness.render();
    CHECK_FALSE(harness.first.lastHeard());
    CHECK_FALSE(harness.second.lastHeard());
}

TEST_CASE("a device forwards its panic to the next device in the chain", "[engine][exec][2418]") {
    ChainHarness harness;
    harness.first.forwards = true;
    harness.prepare();

    harness.render(/*continuous=*/false);
    CHECK(harness.second.lastHeard());

    // And raises one of its own on a block that carried none.
    harness.first.forwards = false;
    harness.first.raises = true;
    harness.render();
    CHECK_FALSE(harness.first.lastHeard());
    CHECK(harness.second.lastHeard());
}

TEST_CASE("a device that says nothing drops the panic it was handed", "[engine][exec][2418]") {
    // What the fork does with its fresh output buffer: a device producing MIDI
    // produces the flag with it, so passing one on is a decision rather than
    // the default.
    ChainHarness harness;
    harness.prepare();

    harness.render(/*continuous=*/false);
    CHECK(harness.first.lastHeard());

    // The second device still hears the locate itself -- every device does --
    // so the port's own answer is what the next block shows.
    harness.render();
    CHECK_FALSE(harness.second.lastHeard());
}

TEST_CASE("a launched slot panics the devices its track feeds", "[engine][exec][2418]") {
    // A discontinuity the transport never moved for: the source chases what
    // sounds at the new position without a note-off for what sounded at the
    // old one (ClipMidiSource.hpp).
    ChainHarness harness;
    harness.prepare();

    harness.render();
    CHECK_FALSE(harness.first.lastHeard());

    harness.source.launched = true;
    harness.render();
    CHECK(harness.first.lastHeard());

    harness.source.launched = false;
    harness.render();
    CHECK_FALSE(harness.first.lastHeard());
}

TEST_CASE("a merge carries a panic from any one of its inputs", "[engine][exec][2418]") {
    MergeHarness harness;
    harness.right.launched = true;
    harness.render();

    CHECK(harness.device.lastHeard());
}

TEST_CASE("a merge with nothing to report carries no panic", "[engine][exec][2418]") {
    MergeHarness harness;
    harness.render();

    CHECK_FALSE(harness.device.lastHeard());
}

TEST_CASE("muting a track raises no panic on its devices", "[engine][exec][2418]") {
    // MAGDA's mute is a gain with the track still rendering, so its meters stay
    // live and nothing is withheld from a device: there is nothing for one to
    // recover from. The fork raises a panic here because it stops the track's
    // contents; carrying the flag without carrying that behaviour would tell a
    // device to drop a chord nothing is going to chase back, and neither the
    // mute nor the unmute leaves the block discontinuous for playLane to chase
    // on (#2418 review).
    auto track = magda::test::makeTrack(1);
    auto instrument = magda::DeviceInfo{};
    instrument.id = 7;
    instrument.name = "Instrument";
    instrument.deviceType = magda::DeviceType::Instrument;
    instrument.isInstrument = true;
    instrument.canReceiveMidi = true;
    instrument.format = magda::PluginFormat::Internal;
    track.chain.fxChainElements.push_back(magda::makeDeviceElement(instrument));

    const auto master = magda::test::makeMaster();
    const auto plan = magda::engine::compileRenderPlan({track}, master, {});

    LaunchingSource source;
    SilentAudio audio;
    PanicProbe probe;
    PlanBindings bindings;
    bindings.clipMidi[1] = &source;
    bindings.clipAudio[1] = &audio;
    bindings.devices[DeviceKey{7}] = &probe;

    PlanExecutor executor;
    const auto messages = executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
    for (const auto& message : messages)
        UNSCOPED_INFO("prepare: " << message);
    REQUIRE(messages.empty());

    juce::AudioBuffer<float> output(2, kBlockSize);
    PlanValues values;

    const auto renderWith = [&](bool muted) {
        auto model = track;
        model.muted = muted;
        const auto valueMessages = magda::engine::resolvePlanValues(plan, {model}, master, values);
        REQUIRE(valueMessages.empty());

        output.clear();
        BlockInfo block;
        block.numSamples = kBlockSize;
        block.playing = true;
        block.continuous = true;
        executor.process(values, block, output);
    };

    renderWith(false);
    REQUIRE_FALSE(probe.heard.empty());
    CHECK_FALSE(probe.lastHeard());

    renderWith(true);
    CHECK_FALSE(probe.lastHeard());

    renderWith(true);
    CHECK_FALSE(probe.lastHeard());

    renderWith(false);
    CHECK_FALSE(probe.lastHeard());
}
