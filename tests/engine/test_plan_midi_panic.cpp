#include <catch2/catch_test_macros.hpp>
#include <vector>

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

    /// Every op of the track, the way the resolver marks them.
    void setTrackInaudible(bool inaudible) {
        for (auto& value : values.ops)
            value.trackInaudible = inaudible;
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

TEST_CASE("the block a track goes inaudible panics its devices once", "[engine][exec][2418]") {
    // The fork's TrackMuteState::wasJustMuted edge. Only the block it changes
    // on: a device panicking every block it spends muted would drop a chord
    // the player is still holding.
    ChainHarness harness;
    harness.prepare();

    harness.render();
    CHECK_FALSE(harness.first.lastHeard());

    harness.setTrackInaudible(true);
    harness.render();
    CHECK(harness.first.lastHeard());

    harness.render();
    CHECK_FALSE(harness.first.lastHeard());

    // Coming back is not a discontinuity: nothing was withheld while the track
    // was muted, because the engine keeps processing it (its meters stay live).
    harness.setTrackInaudible(false);
    harness.render();
    CHECK_FALSE(harness.first.lastHeard());
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

TEST_CASE("mute marks every op of the track inaudible, not just the muting stage",
          "[engine][exec][2418]") {
    // The device is several ops upstream of its TrackMute and reads no gain, so
    // the resolver has to say it on the op rather than only where it silences.
    magda::TrackInfo track;
    track.id = 1;
    track.type = magda::TrackType::Media;
    track.name = "Track 1";
    track.muted = true;

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;
    master.type = magda::TrackType::Master;
    master.name = "Master";

    const auto plan = magda::engine::compileRenderPlan({track}, master, {});
    PlanValues values;
    const auto messages = magda::engine::resolvePlanValues(plan, {track}, master, values);
    for (const auto& message : messages)
        UNSCOPED_INFO("resolve: " << message);
    REQUIRE(messages.empty());

    bool sawTrackOp = false;
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& op = plan.ops[i];
        if (op.key.trackId != 1)
            continue;
        // A delay and a fade have no model location and are left at unity, so
        // they carry no audibility either (PlanValues.cpp).
        if (op.kind == OpKind::Delay || op.kind == OpKind::Crossfade)
            continue;
        sawTrackOp = true;
        UNSCOPED_INFO("op " << i << " " << magda::engine::toString(op.kind));
        CHECK(values.ops[i].trackInaudible);
    }
    CHECK(sawTrackOp);

    // The master is still heard: mute is the track's, and nothing it feeds
    // inherits it upwards.
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].key.trackId == magda::MASTER_TRACK_ID)
            CHECK_FALSE(values.ops[i].trackInaudible);
}
