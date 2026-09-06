#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

using namespace magda;
using magda::engine::CompileOptions;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::RenderPlan;

namespace {

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Media) {
    TrackInfo track;
    track.id = id;
    track.type = type;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID, TrackType::Master);
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeEffect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;

    // What it is, rather than what the struct defaults to. DeviceInfo::format
    // starts at VST3, and these stand for devices MAGDA runs: an external
    // plugin is handed the bus and adapts its own channels, so the width rules
    // below are asked of a device that has declared what it reads (#2246).
    device.format = PluginFormat::Internal;
    return device;
}

DeviceInfo makeInstrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Instrument " + juce::String(id);
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.format = PluginFormat::Internal;
    return device;
}

/// Ops of one kind+role, in plan order.
std::vector<magda::engine::OpId> opsWithRole(const RenderPlan& plan, OpRole role) {
    std::vector<magda::engine::OpId> found;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].key.role == role)
            found.push_back(static_cast<magda::engine::OpId>(i));
    return found;
}

int countRole(const RenderPlan& plan, OpRole role) {
    return static_cast<int>(opsWithRole(plan, role).size());
}

/// The op an input slot reads, or -1 when the slot is unconnected. The two ops
/// every slot carries whatever the model says are looked through: a delay
/// stands for the op behind it, and so does the subtract that would take a
/// delta on it, which passes its wet side along until something is soloing
/// that delta. These tests are about what is routed where; both have their own.
magda::engine::OpId inputOp(const RenderPlan& plan, magda::engine::OpId op, std::size_t slot) {
    const auto& inputs = plan.ops[static_cast<std::size_t>(op)].inputs;
    if (slot >= inputs.size())
        return magda::engine::INVALID_OP_ID;

    auto source = inputs[slot].op;
    while (source != magda::engine::INVALID_OP_ID) {
        const auto& producer = plan.ops[static_cast<std::size_t>(source)];
        if (producer.kind != magda::engine::OpKind::Delay &&
            producer.kind != magda::engine::OpKind::Subtract)
            break;
        source = producer.inputs.front().op;
    }
    return source;
}

/// The op an input slot reads without looking through anything.
magda::engine::OpId rawInputOp(const RenderPlan& plan, magda::engine::OpId op, std::size_t slot) {
    const auto& inputs = plan.ops[static_cast<std::size_t>(op)].inputs;
    return slot >= inputs.size() ? magda::engine::INVALID_OP_ID : inputs[slot].op;
}

/// The op a device's audio actually leaves by. A non-transparent device emits a
/// process op and then its wet/dry gain, so the next stage reads the gain;
/// a transparent tap (analysis devices) emits the process op alone.
magda::engine::OpId deviceOutput(const RenderPlan& plan, DeviceId deviceId) {
    auto found = magda::engine::INVALID_OP_ID;
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (key.deviceId == deviceId &&
            (key.role == OpRole::DeviceProcess || key.role == OpRole::DeviceGain))
            found = static_cast<magda::engine::OpId>(i);
    }
    return found;
}

/// The op feeding a device's audio input.
magda::engine::OpId deviceInput(const RenderPlan& plan, DeviceId deviceId) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (key.deviceId == deviceId && key.role == OpRole::DeviceProcess)
            return inputOp(plan, static_cast<magda::engine::OpId>(i), 0);
    }
    return magda::engine::INVALID_OP_ID;
}

/// Every fixture must compile to a structurally sound plan; this is asserted
/// alongside whatever each test is actually about.
void requireWellFormed(const RenderPlan& plan) {
    const auto problems = magda::engine::validatePlan(plan);
    INFO(magda::engine::dumpPlan(plan));
    for (const auto& problem : problems)
        FAIL_CHECK(problem);
    REQUIRE(problems.empty());
}

CompileOptions withoutDeviceMeters() {
    CompileOptions options;
    options.deviceMeters = false;
    return options;
}

/// What a device's process op declares: what it reads from the bus, and what
/// its main output port carries.
struct DeviceWidths {
    int in = 0;
    int out = 0;
    bool readsTheBus = false;
};

DeviceWidths widthsOf(const RenderPlan& plan, DeviceId deviceId) {
    for (const auto& op : plan.ops) {
        if (op.key.role != OpRole::DeviceProcess || op.key.deviceId != deviceId)
            continue;
        REQUIRE_FALSE(op.outputs.empty());
        return {op.audioInputChannels, op.outputs.front().channels, op.inputs[0].valid()};
    }
    FAIL("plan has no process op for the device");
    return {};
}

/// A mono-in, mono-out effect. The counts come from the live plugin and
/// nowhere else.
DeviceInfo makeMonoEffect(DeviceId id) {
    auto device = makeEffect(id);
    device.audioInputChannels = 1;
    device.audioOutputChannels = 1;
    return device;
}

}  // namespace

TEST_CASE("Empty session compiles to a track feeding the master output",
          "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());

    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());
    CHECK(plan.outputOps.size() == 1);

    // Track 1: clip source, input mix, fader, meter, mute. Master: input mix,
    // fader, meter, mute, hardware output.
    CHECK(countRole(plan, OpRole::ClipAudio) == 1);
    CHECK(countRole(plan, OpRole::TrackAudioInput) == 2);
    CHECK(countRole(plan, OpRole::TrackFader) == 2);
    CHECK(countRole(plan, OpRole::TrackMeter) == 2);
    CHECK(countRole(plan, OpRole::TrackMute) == 2);
    CHECK(countRole(plan, OpRole::HardwareOutput) == 1);

    // The master sums the track's post-mute output.
    const auto masterInput = opsWithRole(plan, OpRole::TrackAudioInput).back();
    const auto trackMute = opsWithRole(plan, OpRole::TrackMute).front();
    CHECK(plan.ops[static_cast<std::size_t>(masterInput)].key.trackId == MASTER_TRACK_ID);
    CHECK(inputOp(plan, masterInput, 0) == trackMute);
}

TEST_CASE("The plan dump is the compiler's golden surface", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK(magda::engine::dumpPlan(plan) == R"(magda-render-plan v1
ops=19 outputs=1
[  0] ClipAudio   det   T1:clipAudio                   in=-                out=audio       deps=0
[  1] SessionAudio det   T1:sessionAudio                in=-                out=audio       deps=0
[  2] Delay       det   T1:mixInputDelay               in=0:0              out=audio       deps=1
[  3] Delay       det   T1:mixInputDelay#1             in=1:0              out=audio       deps=1
[  4] MixAudio    det   T1:trackAudioInput             in=2:0,3:0          out=audio       deps=2
[  5] Device      det   T1/D7:deviceProcess            in=4:0,-,-          out=audio       deps=1
[  6] Delay       det   T1/D7:subtractInputDelay       in=5:0              out=audio       deps=1
[  7] Delay       det   T1/D7:subtractInputDelay#1     in=4:0              out=audio       deps=1
[  8] Subtract    det   T1/D7:deviceDelta              in=6:0,7:0          out=audio       deps=2
[  9] Gain        det   T1/D7:deviceGain               in=8:0              out=audio       deps=1
[ 10] Meter       det   T1/D7:deviceMeter              in=9:0              out=audio       deps=1
[ 11] Fader       det   T1:trackFader                  in=10:0,-           out=audio       deps=1
[ 12] Meter       det   T1:trackMeter                  in=11:0             out=audio       deps=1
[ 13] Gain        det   T1:trackMute                   in=12:0             out=audio       deps=1
[ 14] MixAudio    det   T-2:trackAudioInput            in=13:0             out=audio       deps=1
[ 15] Fader       det   T-2:trackFader                 in=14:0,-           out=audio       deps=1
[ 16] Meter       det   T-2:trackMeter                 in=15:0             out=audio       deps=1
[ 17] Gain        det   T-2:trackMute                  in=16:0             out=audio       deps=1
[ 18] Output      det   T-2:hardwareOutput             in=17:0             out=-           deps=1
ready=0,1
)");
}

TEST_CASE("A second track is a fan-in, and a fan-in is where the delays go",
          "[engine][plan][compiler][pdc]") {
    // The same golden surface with one more track in it. Everything the first
    // track had is unchanged, and what the second one adds is a sum at the
    // master with two inputs and a delay on each: how many samples they hold is
    // not here, because it is not known until the plan is prepared against the
    // plugins it names.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK(magda::engine::dumpPlan(plan) == R"(magda-render-plan v1
ops=29 outputs=1
[  0] ClipAudio   det   T1:clipAudio                   in=-                out=audio       deps=0
[  1] SessionAudio det   T1:sessionAudio                in=-                out=audio       deps=0
[  2] Delay       det   T1:mixInputDelay               in=0:0              out=audio       deps=1
[  3] Delay       det   T1:mixInputDelay#1             in=1:0              out=audio       deps=1
[  4] MixAudio    det   T1:trackAudioInput             in=2:0,3:0          out=audio       deps=2
[  5] Device      det   T1/D7:deviceProcess            in=4:0,-,-          out=audio       deps=1
[  6] Delay       det   T1/D7:subtractInputDelay       in=5:0              out=audio       deps=1
[  7] Delay       det   T1/D7:subtractInputDelay#1     in=4:0              out=audio       deps=1
[  8] Subtract    det   T1/D7:deviceDelta              in=6:0,7:0          out=audio       deps=2
[  9] Gain        det   T1/D7:deviceGain               in=8:0              out=audio       deps=1
[ 10] Meter       det   T1/D7:deviceMeter              in=9:0              out=audio       deps=1
[ 11] Fader       det   T1:trackFader                  in=10:0,-           out=audio       deps=1
[ 12] Meter       det   T1:trackMeter                  in=11:0             out=audio       deps=1
[ 13] Gain        det   T1:trackMute                   in=12:0             out=audio       deps=1
[ 14] ClipAudio   det   T2:clipAudio                   in=-                out=audio       deps=0
[ 15] SessionAudio det   T2:sessionAudio                in=-                out=audio       deps=0
[ 16] Delay       det   T2:mixInputDelay               in=14:0             out=audio       deps=1
[ 17] Delay       det   T2:mixInputDelay#1             in=15:0             out=audio       deps=1
[ 18] MixAudio    det   T2:trackAudioInput             in=16:0,17:0        out=audio       deps=2
[ 19] Fader       det   T2:trackFader                  in=18:0,-           out=audio       deps=1
[ 20] Meter       det   T2:trackMeter                  in=19:0             out=audio       deps=1
[ 21] Gain        det   T2:trackMute                   in=20:0             out=audio       deps=1
[ 22] Delay       det   T-2:mixInputDelay              in=13:0             out=audio       deps=1
[ 23] Delay       det   T-2:mixInputDelay#1            in=21:0             out=audio       deps=1
[ 24] MixAudio    det   T-2:trackAudioInput            in=22:0,23:0        out=audio       deps=2
[ 25] Fader       det   T-2:trackFader                 in=24:0,-           out=audio       deps=1
[ 26] Meter       det   T-2:trackMeter                 in=25:0             out=audio       deps=1
[ 27] Gain        det   T-2:trackMute                  in=26:0             out=audio       deps=1
[ 28] Output      det   T-2:hardwareOutput             in=27:0             out=-           deps=1
ready=0,1,14,15
)");
}

TEST_CASE("Compilation is deterministic", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2, TrackType::Aux)};
    tracks[1].auxBusIndex = 0;
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(3)));
    tracks[0].sends.push_back(SendInfo{0, 0.5f, false, 2});

    const auto first = magda::engine::compileRenderPlan(tracks, makeMaster());
    const auto second = magda::engine::compileRenderPlan(tracks, makeMaster());

    requireWellFormed(first);
    CHECK(magda::engine::dumpPlan(first) == magda::engine::dumpPlan(second));
}

TEST_CASE("A device compiles to process, gain and meter ops", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    SECTION("with device meters") {
        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto process = opsWithRole(plan, OpRole::DeviceProcess);
        const auto gain = opsWithRole(plan, OpRole::DeviceGain);
        const auto meter = opsWithRole(plan, OpRole::DeviceMeter);
        REQUIRE(process.size() == 1);
        REQUIRE(gain.size() == 1);
        REQUIRE(meter.size() == 1);
        CHECK(plan.ops[static_cast<std::size_t>(process.front())].key.deviceId == 7);
        CHECK(inputOp(plan, gain.front(), 0) == process.front());
        CHECK(inputOp(plan, meter.front(), 0) == gain.front());
    }

    SECTION("without device meters") {
        const auto plan =
            magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::DeviceMeter) == 0);
        CHECK(countRole(plan, OpRole::DeviceGain) == 1);
    }
}

TEST_CASE("A device's channel counts reach the ports it is compiled to",
          "[engine][plan][compiler]") {
    SECTION("a stereo device is the bus's width on both sides") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto widths = widthsOf(plan, 7);
        CHECK(widths.in == 2);
        CHECK(widths.out == 2);
    }

    SECTION("a mono device reads and writes one channel") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeMonoEffect(7)));

        const auto plan =
            magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
        requireWellFormed(plan);

        const auto widths = widthsOf(plan, 7);
        CHECK(widths.in == 1);
        CHECK(widths.out == 1);

        // The chain past it is the bus again. A mono port is one channel heard
        // on both sides, not a narrower chain.
        CHECK(inputOp(plan, opsWithRole(plan, OpRole::TrackFader).front(), 0) ==
              deviceOutput(plan, 7));
    }

    SECTION("an external plugin is handed the bus whatever it declares") {
        // The one device the plan has no opinion about (#2246). A hosted plugin
        // that reports one channel is still given both, because the fold and the
        // spread either side of it are the fork's own arithmetic and the adapter
        // reproduces them (EngineExternalDevice.cpp). Narrowing the port first
        // would hand it the left channel and take the average nowhere.
        auto hosted = makeMonoEffect(7);
        hosted.format = PluginFormat::VST3;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(hosted));

        const auto plan =
            magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
        requireWellFormed(plan);

        const auto widths = widthsOf(plan, 7);
        CHECK(widths.in == 2);
        CHECK(widths.out == 2);
    }

    SECTION("a device wider than the bus is clamped to it") {
        auto surround = makeEffect(7);
        surround.audioInputChannels = 6;
        surround.audioOutputChannels = 6;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(surround));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto widths = widthsOf(plan, 7);
        CHECK(widths.in == 2);
        CHECK(widths.out == 2);
    }

    SECTION("an analysis tap keeps the bus whatever it reports") {
        auto scope = makeMonoEffect(9);
        scope.deviceType = DeviceType::Analysis;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(scope));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        // Its output is its input, so narrowing it would narrow the chain.
        const auto widths = widthsOf(plan, 9);
        CHECK(widths.in == 2);
        CHECK(widths.out == 2);
    }
}

TEST_CASE("A device with no audio input is not wired to the bus", "[engine][plan][compiler]") {
    auto generator = makeEffect(7);
    generator.audioInputChannels = 0;

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(generator));
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    const auto widths = widthsOf(plan, 7);
    CHECK(widths.in == 0);
    CHECK_FALSE(widths.readsTheBus);

    // The bus flows past it, the way the current engine treats a device it
    // never connects. The effect behind it reads the track's audio input
    // rather than the generator.
    CHECK(deviceInput(plan, 8) == opsWithRole(plan, OpRole::TrackAudioInput).front());

    // It still has its own slot, which goes nowhere. The device runs and its
    // meter reads it, but nothing downstream hears it.
    CHECK(countRole(plan, OpRole::DeviceMeter) == 2);
}

TEST_CASE("A device with no audio output starves what follows it", "[engine][plan][compiler]") {
    auto sink = makeEffect(7);
    sink.audioOutputChannels = 0;

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(sink));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    const auto widths = widthsOf(plan, 7);
    CHECK(widths.in == 2);
    CHECK(widths.out == 0);

    // Nothing to trim, nothing to meter, and nothing for the fader to read.
    // The current engine clears the bus at its input and puts nothing back.
    CHECK(countRole(plan, OpRole::DeviceGain) == 0);
    CHECK(countRole(plan, OpRole::DeviceMeter) == 0);
    CHECK(rawInputOp(plan, opsWithRole(plan, OpRole::TrackFader).front(), 0) ==
          magda::engine::INVALID_OP_ID);
}

TEST_CASE("An instrument's output sums into the bus flowing past it", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
    requireWellFormed(plan);

    // It is never given the bus. An instrument generates rather than
    // processes, so nothing is connected to its audio input.
    const auto widths = widthsOf(plan, 8);
    CHECK(widths.in == 0);
    CHECK_FALSE(widths.readsTheBus);

    const auto inject = opsWithRole(plan, OpRole::DeviceInject);
    REQUIRE(inject.size() == 1);
    CHECK(plan.ops[static_cast<std::size_t>(inject.front())].kind == OpKind::MixAudio);

    // It sums two things: what flowed past, and the instrument's slot. The
    // slot rather than the process op, because the trim and meter belong to
    // the plugin and run before the sum.
    CHECK(inputOp(plan, inject.front(), 0) == deviceOutput(plan, 7));
    CHECK(rawInputOp(plan, inject.front(), 1) != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, inject.front(), 1) == deviceOutput(plan, 8));

    CHECK(inputOp(plan, opsWithRole(plan, OpRole::TrackFader).front(), 0) == inject.front());
}

TEST_CASE("An instrument behind a starved bus has nothing to sum with",
          "[engine][plan][compiler]") {
    auto sink = makeEffect(7);
    sink.audioOutputChannels = 0;

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(sink));
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
    requireWellFormed(plan);

    // Nothing flowed past for it to join, so there is no mix to emit and the
    // chain carries on from the instrument's slot alone.
    CHECK(countRole(plan, OpRole::DeviceInject) == 0);
    CHECK(inputOp(plan, opsWithRole(plan, OpRole::TrackFader).front(), 0) == deviceOutput(plan, 8));
}

TEST_CASE("Analysis devices compile to a bare process op", "[engine][plan][compiler]") {
    auto scope = makeEffect(9);
    scope.deviceType = DeviceType::Analysis;

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(scope));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK(countRole(plan, OpRole::DeviceProcess) == 1);
    CHECK(countRole(plan, OpRole::DeviceGain) == 0);
    CHECK(countRole(plan, OpRole::DeviceMeter) == 0);
}

TEST_CASE("A MIDI source is compiled only when the chain consumes MIDI",
          "[engine][plan][compiler]") {
    SECTION("an effect-only chain has no MIDI source") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::ClipMidi) == 0);
        CHECK(countRole(plan, OpRole::TrackMidiInput) == 0);
    }

    SECTION("an instrument pulls the track's MIDI clips into the plan") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(3)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        REQUIRE(countRole(plan, OpRole::ClipMidi) == 1);
        REQUIRE(countRole(plan, OpRole::TrackMidiInput) == 1);

        const auto instrument = opsWithRole(plan, OpRole::DeviceProcess).front();
        const auto midiInput = opsWithRole(plan, OpRole::TrackMidiInput).front();
        CHECK(inputOp(plan, instrument, 1) == midiInput);
    }
}

TEST_CASE("Bypass and chain power remove devices from the plan", "[engine][plan][compiler]") {
    auto bypassed = makeEffect(7);
    bypassed.bypassed = true;

    SECTION("a bypassed device is not compiled") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(bypassed));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::DeviceProcess) == 0);
    }

    SECTION("chain power gates the insert chain but not post-FX") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.enabled = false;
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
        tracks[0].chain.postFxChainElements.push_back(PostFxChainElement{makeEffect(8)});

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto process = opsWithRole(plan, OpRole::DeviceProcess);
        REQUIRE(process.size() == 1);
        CHECK(plan.ops[static_cast<std::size_t>(process.front())].key.deviceId == 8);
    }
}

TEST_CASE("A rack compiles to per-chain faders, a mix and a rack fader",
          "[engine][plan][compiler]") {
    RackInfo rack;
    rack.id = 4;
    for (ChainId chainId : {10, 11}) {
        ChainInfo chain;
        chain.id = chainId;
        chain.elements.push_back(makeDeviceElement(makeEffect(chainId + 90)));
        rack.chains.push_back(std::move(chain));
    }

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
    requireWellFormed(plan);

    CHECK(countRole(plan, OpRole::RackChainFader) == 2);
    CHECK(countRole(plan, OpRole::RackMix) == 1);
    CHECK(countRole(plan, OpRole::RackFader) == 1);

    // The rack mix sums the chain faders in compiled chain order, and the rack
    // fader reads the mix.
    const auto chainFaders = opsWithRole(plan, OpRole::RackChainFader);
    const auto mix = opsWithRole(plan, OpRole::RackMix).front();
    CHECK(inputOp(plan, mix, 0) == chainFaders[0]);
    CHECK(inputOp(plan, mix, 1) == chainFaders[1]);
    CHECK(inputOp(plan, opsWithRole(plan, OpRole::RackFader).front(), 0) == mix);

    // Devices inside a rack are keyed by their enclosing rack and chain, which
    // is what lets the differ tell two copies of the same device apart.
    const auto devices = opsWithRole(plan, OpRole::DeviceProcess);
    REQUIRE(devices.size() == 2);
    CHECK(plan.ops[static_cast<std::size_t>(devices[0])].key.rackId == 4);
    CHECK(plan.ops[static_cast<std::size_t>(devices[0])].key.chainId == 10);
    CHECK(plan.ops[static_cast<std::size_t>(devices[1])].key.chainId == 11);
}

TEST_CASE("Sends are compiled after their source track", "[engine][plan][compiler]") {
    // The aux is declared first in project order, so only the send dependency
    // can put it after the source track.
    std::vector<TrackInfo> tracks{makeTrack(2, TrackType::Aux), makeTrack(1)};
    tracks[0].auxBusIndex = 0;
    tracks[1].sends.push_back(SendInfo{0, 0.5f, false, 2});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    const auto sendTaps = opsWithRole(plan, OpRole::SendTap);
    REQUIRE(sendTaps.size() == 1);

    const auto sourceFader = opsWithRole(plan, OpRole::TrackFader).front();
    CHECK(plan.ops[static_cast<std::size_t>(sourceFader)].key.trackId == 1);
    // A post-fader send taps the fader output.
    CHECK(inputOp(plan, sendTaps.front(), 0) == sourceFader);

    // The aux track's input mix reads the send tap.
    magda::engine::OpId auxInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            auxInput = op;
    REQUIRE(auxInput != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, auxInput, 0) == sendTaps.front());
}

TEST_CASE("A pre-fader send taps the signal before the fader", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2, TrackType::Aux)};
    tracks[1].auxBusIndex = 0;
    tracks[0].sends.push_back(SendInfo{0, 1.0f, true, 2});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    const auto sendTap = opsWithRole(plan, OpRole::SendTap).front();
    const auto trackInput = opsWithRole(plan, OpRole::TrackAudioInput).front();
    CHECK(inputOp(plan, sendTap, 0) == trackInput);
}

TEST_CASE("Group children are summed into the group track", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(5, TrackType::Group), makeTrack(1), makeTrack(2)};
    tracks[0].childIds = {1, 2};
    for (int i = 1; i <= 2; ++i) {
        tracks[static_cast<std::size_t>(i)].parentId = 5;
        tracks[static_cast<std::size_t>(i)].audioOutputDevice = "track:5";
    }

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    magda::engine::OpId groupInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 5)
            groupInput = op;
    REQUIRE(groupInput != magda::engine::INVALID_OP_ID);

    // Both children, in project order, and nothing else: a group carries no
    // clips of its own.
    REQUIRE(plan.ops[static_cast<std::size_t>(groupInput)].inputs.size() == 2);
    const auto mutes = opsWithRole(plan, OpRole::TrackMute);
    CHECK(inputOp(plan, groupInput, 0) == mutes[0]);
    CHECK(inputOp(plan, groupInput, 1) == mutes[1]);

    // The master sums the group only; the children route into it, not past it.
    magda::engine::OpId masterInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == MASTER_TRACK_ID)
            masterInput = op;
    REQUIRE(masterInput != magda::engine::INVALID_OP_ID);
    CHECK(plan.ops[static_cast<std::size_t>(masterInput)].inputs.size() == 1);
}

TEST_CASE("An audio sidechain wires the source track's output into the device",
          "[engine][plan][compiler]") {
    auto compressor = makeEffect(7);
    compressor.canSidechain = true;
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;

    // The sidechained track is declared first; only the sidechain dependency
    // can reorder it after its source.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    const auto device = opsWithRole(plan, OpRole::DeviceProcess).front();
    magda::engine::OpId sourceMeter = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMeter))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            sourceMeter = op;
    REQUIRE(sourceMeter != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, device, 2) == sourceMeter);
}

TEST_CASE("Liveness propagates downstream from a live input", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].recordArmed = true;
    tracks[0].audioInputDevice = "Input 1";

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    const auto liveOf = [&plan](magda::engine::OpId op) {
        return plan.ops[static_cast<std::size_t>(op)].liveness ==
               magda::engine::LivenessDomain::Live;
    };

    REQUIRE(countRole(plan, OpRole::LiveAudioInput) == 1);
    CHECK(liveOf(opsWithRole(plan, OpRole::LiveAudioInput).front()));

    // Track 1's chain and everything downstream of it, including the master.
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput)) {
        const auto trackId = plan.ops[static_cast<std::size_t>(op)].key.trackId;
        CHECK(liveOf(op) == (trackId == 1 || trackId == MASTER_TRACK_ID));
    }
    // Track 2 never touches the live signal.
    for (const auto op : opsWithRole(plan, OpRole::ClipAudio))
        CHECK_FALSE(liveOf(op));
    CHECK(liveOf(plan.outputOps.front()));
}

TEST_CASE("The chord track is what a plan for a file leaves out", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(9, TrackType::Chord)};

    // The default, which is what a bounce compiles under. Asserted as the
    // default rather than by asking for it: a caller that forgets to say what
    // its plan is for must not print a chord track into somebody's master
    // (#2446).
    const auto bounce = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(bounce);

    for (const auto& op : bounce.ops)
        CHECK(op.key.trackId != 9);
}

TEST_CASE("The chord track is in a plan for playback", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(9, TrackType::Chord)};

    // Its mute is the audition toggle, so a playback plan that left it out
    // would be a chord track that never sounds (#2446).
    magda::engine::CompileOptions options;
    options.monitorTracks = true;

    const auto playback = magda::engine::compileRenderPlan(tracks, makeMaster(), options);
    requireWellFormed(playback);

    auto reached = false;
    for (const auto& op : playback.ops)
        reached = reached || op.key.trackId == 9;
    CHECK(reached);

    // And the plan is larger for it, rather than the track being named by an
    // op the default plan already had.
    CHECK(playback.ops.size() > magda::engine::compileRenderPlan(tracks, makeMaster()).ops.size());
}

TEST_CASE("A send cycle is broken and reported", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1, TrackType::Aux), makeTrack(2, TrackType::Aux)};
    tracks[0].auxBusIndex = 0;
    tracks[1].auxBusIndex = 1;
    tracks[0].sends.push_back(SendInfo{1, 1.0f, false, 2});
    tracks[1].sends.push_back(SendInfo{0, 1.0f, false, 1});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());

    // The plan still has to be executable: a cycle costs a connection, never
    // plan validity.
    requireWellFormed(plan);
    CHECK_FALSE(plan.diagnostics.empty());
    CHECK(plan.outputOps.size() == 1);
}

TEST_CASE("A send with no destination is reported", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].sends.push_back(SendInfo{3, 1.0f, false, INVALID_TRACK_ID});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    REQUIRE(plan.diagnostics.size() == 1);
    CHECK(plan.diagnostics.front().find("aux bus 3") != std::string::npos);
    CHECK(countRole(plan, OpRole::SendTap) == 0);
}

TEST_CASE("Scheduling constants match the compiled dependencies", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2, TrackType::Aux)};
    tracks[1].auxBusIndex = 0;
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(3)));
    tracks[0].sends.push_back(SendInfo{0, 0.5f, false, 2});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    REQUIRE(plan.dependencyCounts.size() == plan.ops.size());
    REQUIRE(plan.consumerOffsets.size() == plan.ops.size() + 1);

    // Every op is reachable from the initial ready set by counting down its
    // dependencies exactly once through the consumer edges: the executor's
    // per-block loop, run here at compile time.
    auto countdown = plan.dependencyCounts;
    std::vector<magda::engine::OpId> queue = plan.initialReadyOps;
    std::size_t processed = 0;
    while (processed < queue.size()) {
        const auto op = queue[processed++];
        const auto begin = plan.consumerOffsets[static_cast<std::size_t>(op)];
        const auto end = plan.consumerOffsets[static_cast<std::size_t>(op) + 1];
        for (auto edge = begin; edge < end; ++edge) {
            const auto consumer = plan.consumerEdges[static_cast<std::size_t>(edge)];
            if (--countdown[static_cast<std::size_t>(consumer)] == 0)
                queue.push_back(consumer);
        }
    }
    CHECK(queue.size() == plan.ops.size());
    CHECK(std::ranges::all_of(countdown, [](auto count) { return count == 0; }));
}

TEST_CASE("An empty rack passes signal through its fader", "[engine][plan][compiler]") {
    RackInfo rack;
    rack.id = 4;

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    // Compiling an empty rack to a zero-input mix would silence the track, and
    // there is nothing to sum. Rack volume and pan still apply though: the
    // current engine writes them to the instance output gains regardless of
    // chains, and they sit on the wet path.
    CHECK(countRole(plan, OpRole::RackMix) == 0);
    REQUIRE(countRole(plan, OpRole::RackFader) == 1);

    const auto trackInput = opsWithRole(plan, OpRole::TrackAudioInput).front();
    const auto rackFader = opsWithRole(plan, OpRole::RackFader).front();
    CHECK(inputOp(plan, rackFader, 0) == trackInput);
    CHECK(inputOp(plan, opsWithRole(plan, OpRole::TrackFader).front(), 0) == rackFader);
}

TEST_CASE("A bypassed rack is transparent", "[engine][plan][compiler]") {
    RackInfo rack;
    rack.id = 4;
    rack.bypassed = true;
    ChainInfo chain;
    chain.id = 10;
    chain.elements.push_back(makeDeviceElement(makeEffect(7)));
    rack.chains.push_back(std::move(chain));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK(countRole(plan, OpRole::DeviceProcess) == 0);
    CHECK(countRole(plan, OpRole::RackMix) == 0);
    const auto trackInput = opsWithRole(plan, OpRole::TrackAudioInput).front();
    CHECK(inputOp(plan, opsWithRole(plan, OpRole::TrackFader).front(), 0) == trackInput);
}

TEST_CASE("Nested racks compile through the outer rack's chain", "[engine][plan][compiler]") {
    RackInfo inner;
    inner.id = 8;
    ChainInfo innerChain;
    innerChain.id = 20;
    innerChain.elements.push_back(makeDeviceElement(makeEffect(7)));
    inner.chains.push_back(std::move(innerChain));

    RackInfo outer;
    outer.id = 4;
    ChainInfo outerChain;
    outerChain.id = 10;
    outerChain.elements.push_back(makeRackElement(std::move(inner)));
    outer.chains.push_back(std::move(outerChain));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
    requireWellFormed(plan);

    CHECK(countRole(plan, OpRole::RackMix) == 2);
    CHECK(countRole(plan, OpRole::RackFader) == 2);

    // The device is keyed by its innermost rack and chain, not the outer ones.
    const auto device = opsWithRole(plan, OpRole::DeviceProcess).front();
    CHECK(plan.ops[static_cast<std::size_t>(device)].key.rackId == 8);
    CHECK(plan.ops[static_cast<std::size_t>(device)].key.chainId == 20);

    // Inner rack fader feeds the outer chain fader, which feeds the outer mix.
    const auto rackFaders = opsWithRole(plan, OpRole::RackFader);
    const auto chainFaders = opsWithRole(plan, OpRole::RackChainFader);
    REQUIRE(chainFaders.size() == 2);
    CHECK(inputOp(plan, chainFaders[1], 0) == rackFaders[0]);
}

TEST_CASE("A MIDI sidechain reads the source track's chain-head MIDI", "[engine][plan][compiler]") {
    auto gate = makeEffect(7);
    gate.canReceiveMidi = true;
    gate.sidechain.type = SidechainConfig::Type::MIDI;
    gate.sidechain.sourceTrackId = 2;

    // Track 2 has no instrument of its own, so nothing in its chain consumes
    // MIDI; its clips still have to be compiled because track 1 reads them.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(gate));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    magda::engine::OpId sourceMidi = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMidiInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            sourceMidi = op;
    REQUIRE(sourceMidi != magda::engine::INVALID_OP_ID);

    const auto device = opsWithRole(plan, OpRole::DeviceProcess).front();
    CHECK(inputOp(plan, device, 1) == sourceMidi);
}

TEST_CASE("An internal MIDI route reads the source track's MIDI", "[engine][plan][compiler]") {
    // Declared before its source, so only the routing dependency can order it.
    std::vector<TrackInfo> tracks{makeTrack(2), makeTrack(1)};
    tracks[0].midiInputDevice = "track:1";
    tracks[0].inputMonitor = InputMonitorMode::In;
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(3)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    // The route is internal, so it must not compile to a hardware MIDI input.
    CHECK(countRole(plan, OpRole::LiveMidiInput) == 0);

    magda::engine::OpId sourceMidi = magda::engine::INVALID_OP_ID;
    magda::engine::OpId destMidi = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMidiInput)) {
        const auto trackId = plan.ops[static_cast<std::size_t>(op)].key.trackId;
        if (trackId == 1)
            sourceMidi = op;
        if (trackId == 2)
            destMidi = op;
    }
    REQUIRE(sourceMidi != magda::engine::INVALID_OP_ID);
    REQUIRE(destMidi != magda::engine::INVALID_OP_ID);

    // Own clips first, arrangement then session, and the routed source behind
    // them.
    REQUIRE(plan.ops[static_cast<std::size_t>(destMidi)].inputs.size() == 3);
    CHECK(inputOp(plan, destMidi, 2) == sourceMidi);
}

TEST_CASE("An internal audio route reads the source track's post-mute output",
          "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(2), makeTrack(1)};
    tracks[0].audioInputDevice = "track:1";
    tracks[0].recordArmed = true;

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    CHECK(countRole(plan, OpRole::LiveAudioInput) == 0);

    magda::engine::OpId sourceMute = magda::engine::INVALID_OP_ID;
    magda::engine::OpId destInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMute))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 1)
            sourceMute = op;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            destInput = op;
    REQUIRE(sourceMute != magda::engine::INVALID_OP_ID);
    REQUIRE(destInput != magda::engine::INVALID_OP_ID);

    REQUIRE(plan.ops[static_cast<std::size_t>(destInput)].inputs.size() == 3);
    CHECK(inputOp(plan, destInput, 2) == sourceMute);
}

TEST_CASE("Mute is applied after the meter and the sidechain tap", "[engine][plan][compiler]") {
    // The current engine sends to the sidechain bus and taps the meter before
    // the muting node, so a muted track still keys a compressor and still reads
    // on its own meter.
    auto compressor = makeEffect(7);
    compressor.canSidechain = true;
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;

    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));
    tracks[1].muted = true;

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    const auto muteOf = [&plan](magda::TrackId trackId) {
        for (const auto op : opsWithRole(plan, OpRole::TrackMute))
            if (plan.ops[static_cast<std::size_t>(op)].key.trackId == trackId)
                return op;
        return magda::engine::INVALID_OP_ID;
    };
    const auto meterOf = [&plan](magda::TrackId trackId) {
        for (const auto op : opsWithRole(plan, OpRole::TrackMeter))
            if (plan.ops[static_cast<std::size_t>(op)].key.trackId == trackId)
                return op;
        return magda::engine::INVALID_OP_ID;
    };

    // fader -> meter -> mute, per track.
    REQUIRE(muteOf(2) != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, muteOf(2), 0) == meterOf(2));

    // The sidechain taps the meter, not the mute.
    const auto device = opsWithRole(plan, OpRole::DeviceProcess).front();
    CHECK(inputOp(plan, device, 2) == meterOf(2));

    // What reaches the master is post-mute. Track 2 is summed first because the
    // sidechain puts it ahead of track 1 in compile order.
    magda::engine::OpId masterInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == MASTER_TRACK_ID)
            masterInput = op;
    REQUIRE(masterInput != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, masterInput, 0) == muteOf(2));
    CHECK(inputOp(plan, masterInput, 1) == muteOf(1));
}

TEST_CASE("A sidechain on an inactive device is not an ordering dependency",
          "[engine][plan][compiler]") {
    // Track 1 sends to track 2, and a bypassed device on track 1 reads track 2.
    // Only the send is real; counting the dead sidechain as a dependency would
    // invent a cycle and cost the send.
    auto bypassed = makeEffect(7);
    bypassed.bypassed = true;
    bypassed.canSidechain = true;
    bypassed.sidechain.type = SidechainConfig::Type::Audio;
    bypassed.sidechain.sourceTrackId = 2;

    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2, TrackType::Aux)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(bypassed));
    tracks[1].auxBusIndex = 0;
    tracks[0].sends.push_back(SendInfo{0, 1.0f, false, 2});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK(plan.diagnostics.empty());
    REQUIRE(countRole(plan, OpRole::SendTap) == 1);

    magda::engine::OpId auxInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            auxInput = op;
    REQUIRE(auxInput != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, auxInput, 0) == opsWithRole(plan, OpRole::SendTap).front());
}

TEST_CASE("Chain power gates sidechain dependencies too", "[engine][plan][compiler]") {
    auto compressor = makeEffect(7);
    compressor.canSidechain = true;
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;

    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2, TrackType::Aux)};
    tracks[0].chain.enabled = false;
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));
    tracks[1].auxBusIndex = 0;
    tracks[0].sends.push_back(SendInfo{0, 1.0f, false, 2});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());
    CHECK(countRole(plan, OpRole::SendTap) == 1);
}

TEST_CASE("A malformed track route is reported, not reinterpreted", "[engine][plan][compiler]") {
    SECTION("as an audio input") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].audioInputDevice = "track:1-2";
        tracks[0].recordArmed = true;

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        // Falling back to a hardware input would put a live op in the plan for
        // a route the user never asked for.
        CHECK(countRole(plan, OpRole::LiveAudioInput) == 0);
        REQUIRE(plan.diagnostics.size() == 1);
        CHECK(plan.diagnostics.front().find("track:1-2") != std::string::npos);
    }

    SECTION("as a MIDI input") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].midiInputDevice = "track:";
        tracks[0].inputMonitor = InputMonitorMode::In;

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        CHECK(countRole(plan, OpRole::LiveMidiInput) == 0);
        REQUIRE(plan.diagnostics.size() == 1);
        CHECK(plan.diagnostics.front().find("MIDI input routing") != std::string::npos);
    }
}

TEST_CASE("An inactive internal route is not an ordering dependency", "[engine][plan][compiler]") {
    // Track 2 has an input from track 1 but is neither armed nor monitoring, so
    // it reads nothing. Track 1 sidechains from track 2. Counting the dead
    // route as a dependency would close a cycle and cost the live sidechain.
    auto compressor = makeEffect(7);
    compressor.canSidechain = true;
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;

    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));
    tracks[1].audioInputDevice = "track:1";
    tracks[1].recordArmed = false;
    tracks[1].inputMonitor = InputMonitorMode::Off;

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    // Track 2 is compiled first and its output reaches the sidechain slot.
    const auto device = opsWithRole(plan, OpRole::DeviceProcess).front();
    magda::engine::OpId sourceMeter = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMeter))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            sourceMeter = op;
    REQUIRE(sourceMeter != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, device, 2) == sourceMeter);
}

TEST_CASE("An unmonitored MIDI route does not make its source compile MIDI",
          "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[1].midiInputDevice = "track:1";
    tracks[1].inputMonitor = InputMonitorMode::Off;

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    // Track 1 has no MIDI consumer of its own and nothing reads its MIDI, so
    // compiling clip MIDI for it would leave ops no one consumes.
    CHECK(countRole(plan, OpRole::ClipMidi) == 0);
    CHECK(countRole(plan, OpRole::TrackMidiInput) == 0);
}

TEST_CASE("A rack-level sidechain is an edge to the modulation system",
          "[engine][plan][compiler]") {
    // The rack, sidechained from track 2, with whatever modifiers the caller
    // puts on it.
    const auto planWith = [](magda::ModArray mods, SidechainConfig::Type type) {
        RackInfo rack;
        rack.id = 4;
        rack.sidechain.type = type;
        rack.sidechain.sourceTrackId = 2;
        rack.mods = std::move(mods);
        ChainInfo chain;
        chain.id = 10;
        chain.elements.push_back(makeDeviceElement(makeEffect(7)));
        rack.chains.push_back(std::move(chain));

        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

        return magda::engine::compileRenderPlan(tracks, makeMaster());
    };

    const auto listening = [](magda::ModType type, magda::LFOTriggerMode trigger) {
        magda::ModInfo mod(0);
        mod.type = type;
        mod.triggerMode = trigger;
        return magda::ModArray{mod};
    };

    SECTION("a rack nothing listens with is not an edge") {
        // A sidechain feeds modulation, so a rack whose modifiers all
        // free-run is a sidechain with nothing on the far end. No tap, and no
        // diagnostic either: there is nothing the plan failed to carry.
        const auto plan = planWith({}, SidechainConfig::Type::MIDI);
        requireWellFormed(plan);

        CHECK(plan.diagnostics.empty());
        CHECK(countRole(plan, OpRole::ModulationTap) == 0);
    }

    SECTION("a note-triggered modifier on the rack makes it one") {
        const auto plan = planWith(listening(magda::ModType::LFO, magda::LFOTriggerMode::MIDI),
                                   SidechainConfig::Type::MIDI);
        requireWellFormed(plan);

        CHECK(plan.diagnostics.empty());
        REQUIRE(countRole(plan, OpRole::ModulationTap) == 1);

        // Keyed to the source rather than to the rack: what the op carries is
        // one track's signal, and every modifier listening to that track reads
        // the same one.
        const auto taps = opsWithRole(plan, OpRole::ModulationTap);
        REQUIRE(taps.size() == 1);
        CHECK(plan.ops[static_cast<std::size_t>(taps.front())].key.trackId == 2);
    }

    SECTION("a follower on the rack makes it one too") {
        const auto plan = planWith(listening(magda::ModType::Follower, magda::LFOTriggerMode::Free),
                                   SidechainConfig::Type::Audio);
        requireWellFormed(plan);

        CHECK(plan.diagnostics.empty());
        CHECK(countRole(plan, OpRole::ModulationTap) == 1);
    }
}

TEST_CASE("Every device slot and every rack carries the subtract a delta is taken on",
          "[engine][plan][compiler]") {
    SECTION("the flag does not change the plan at all") {
        // Delta solo is a value, not structure. The subtract and the delay
        // feeding it are in the plan whatever the model says, because that
        // delay is a delay line and a delay line is history: one created at the
        // moment the button was pressed would hand back its own length in
        // silence first. So the two plans are the same plan, and the toggle is
        // a value away.
        std::vector<TrackInfo> off{makeTrack(1)};
        off[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        auto soloed = makeEffect(7);
        soloed.deltaSolo = true;
        std::vector<TrackInfo> on{makeTrack(1)};
        on[0].chain.fxChainElements.push_back(makeDeviceElement(soloed));

        const auto planOff = magda::engine::compileRenderPlan(off, makeMaster());
        const auto planOn = magda::engine::compileRenderPlan(on, makeMaster());
        requireWellFormed(planOff);

        CHECK(magda::engine::dumpPlan(planOff) == magda::engine::dumpPlan(planOn));
        CHECK(magda::engine::planFingerprint(planOff) == magda::engine::planFingerprint(planOn));
    }

    SECTION("a device is measured against the audio it was handed") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());

        const auto deltas = opsWithRole(plan, OpRole::DeviceDelta);
        REQUIRE(deltas.size() == 1);
        const auto delta = deltas.front();
        CHECK(plan.ops[static_cast<std::size_t>(delta)].kind == OpKind::Subtract);

        const auto process = opsWithRole(plan, OpRole::DeviceProcess).front();
        CHECK(inputOp(plan, delta, 0) == process);
        CHECK(inputOp(plan, delta, 1) == deviceInput(plan, 7));

        // In front of the slot's gain and meter, which read the delta rather
        // than the device whenever one is being heard.
        CHECK(rawInputOp(plan, opsWithRole(plan, OpRole::DeviceGain).front(), 0) == delta);
    }

    SECTION("the dry edge is taken in front of the device's own input alignment") {
        auto compressor = makeEffect(7);
        compressor.sidechain.type = SidechainConfig::Type::Audio;
        compressor.sidechain.sourceTrackId = 2;

        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());

        // A sidechain gives the device's audio slot a delay of its own. The
        // delta's dry delay hangs off the same producer rather than off that
        // one: a delay reading a delay would count the edge twice, and
        // validatePlan rejects it, which requireWellFormed has just asserted.
        const auto delta = opsWithRole(plan, OpRole::DeviceDelta).front();
        const auto process = opsWithRole(plan, OpRole::DeviceProcess).front();
        const auto dryDelay = rawInputOp(plan, delta, 1);
        const auto deviceDelay = rawInputOp(plan, process, 0);
        REQUIRE(dryDelay != magda::engine::INVALID_OP_ID);
        REQUIRE(deviceDelay != magda::engine::INVALID_OP_ID);
        CHECK(plan.ops[static_cast<std::size_t>(dryDelay)].kind == OpKind::Delay);
        CHECK(plan.ops[static_cast<std::size_t>(deviceDelay)].kind == OpKind::Delay);
        CHECK(dryDelay != deviceDelay);
        CHECK(plan.ops[static_cast<std::size_t>(dryDelay)].inputs.front() ==
              plan.ops[static_cast<std::size_t>(deviceDelay)].inputs.front());
    }

    SECTION("a rack is measured one level up, around its own fader") {
        RackInfo rack;
        rack.id = 4;
        ChainInfo chain;
        chain.id = 10;
        chain.elements.push_back(makeDeviceElement(makeEffect(7)));
        rack.chains.push_back(std::move(chain));

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());

        const auto deltas = opsWithRole(plan, OpRole::RackDelta);
        REQUIRE(deltas.size() == 1);
        const auto delta = deltas.front();
        CHECK(plan.ops[static_cast<std::size_t>(delta)].kind == OpKind::Subtract);
        CHECK(plan.ops[static_cast<std::size_t>(delta)].key.rackId == 4);

        // The rack's volume and pan are on the wet path in the current engine
        // whether or not a delta is being heard, so the fader is inside what is
        // measured; the dry side is what reached the rack.
        CHECK(inputOp(plan, delta, 0) == opsWithRole(plan, OpRole::RackFader).front());

        const auto dry = inputOp(plan, delta, 1);
        REQUIRE(dry != magda::engine::INVALID_OP_ID);
        CHECK(plan.ops[static_cast<std::size_t>(dry)].key.role == OpRole::TrackAudioInput);
        CHECK(plan.ops[static_cast<std::size_t>(dry)].key.trackId == 1);
    }

    SECTION("a rack with no chains still has an output of its own to measure") {
        RackInfo rack;
        rack.id = 4;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());

        REQUIRE(countRole(plan, OpRole::RackDelta) == 1);
        const auto delta = opsWithRole(plan, OpRole::RackDelta).front();
        CHECK(inputOp(plan, delta, 0) == opsWithRole(plan, OpRole::RackFader).front());

        const auto dry = inputOp(plan, delta, 1);
        REQUIRE(dry != magda::engine::INVALID_OP_ID);
        CHECK(plan.ops[static_cast<std::size_t>(dry)].key.role == OpRole::TrackAudioInput);
        CHECK(plan.ops[static_cast<std::size_t>(dry)].key.trackId == 1);
    }

    SECTION("a device inside a rack inside a rack is keyed where it sits") {
        RackInfo inner;
        inner.id = 5;
        ChainInfo innerChain;
        innerChain.id = 11;
        innerChain.elements.push_back(makeDeviceElement(makeEffect(7)));
        inner.chains.push_back(std::move(innerChain));

        RackInfo outer;
        outer.id = 4;
        ChainInfo outerChain;
        outerChain.id = 10;
        outerChain.elements.push_back(makeRackElement(std::move(inner)));
        outer.chains.push_back(std::move(outerChain));

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(outer)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());

        // The dry edge crosses no boundary the keys do not already carry: it is
        // the chain audio at the device's own site, which is where the device
        // op is keyed too.
        REQUIRE(countRole(plan, OpRole::DeviceDelta) == 1);
        const auto delta = opsWithRole(plan, OpRole::DeviceDelta).front();
        const auto& key = plan.ops[static_cast<std::size_t>(delta)].key;
        CHECK(key.trackId == 1);
        CHECK(key.rackId == 5);
        CHECK(key.chainId == 11);
        CHECK(key.deviceId == 7);
        CHECK(inputOp(plan, delta, 0) == opsWithRole(plan, OpRole::DeviceProcess).front());
        CHECK(inputOp(plan, delta, 1) == deviceInput(plan, 7));

        // Both racks are measured too, each around its own fader.
        CHECK(countRole(plan, OpRole::RackDelta) == 2);
    }

    SECTION("a transparent tap has nothing to measure") {
        // Its output is its input, so its delta is silence by construction and
        // there is no difference for a subtract to find. The same reason it has
        // no gain trim and no meter of its own.
        auto analysis = makeEffect(7);
        analysis.deviceType = DeviceType::Analysis;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(analysis));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::DeviceDelta) == 0);
    }

    SECTION("a bypassed device is not in the plan, so nothing measures it") {
        auto effect = makeEffect(7);
        effect.deltaSolo = true;
        effect.bypassed = true;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(effect));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        // The chain, not silence, which is parity: bypass reaches the current
        // engine as `Plugin::setEnabled(false)`, and PluginNode skips the
        // subtraction for a plugin that is not enabled. Delta solo on a device
        // that does nothing is silence because subtracting its input from its
        // output leaves nothing, not because the flag is on.
        CHECK(countRole(plan, OpRole::DeviceDelta) == 0);

        // And the pair is reported, because it is a pair the model's own
        // setters refuse to make: this one was written by hand, the way
        // deserialisation writes it.
        REQUIRE(plan.diagnostics.size() == 1);
        CHECK(plan.diagnostics.front().find("delta solo on a bypassed device measures nothing") !=
              std::string::npos);
    }

    SECTION("a bypassed rack is the same one level up") {
        RackInfo rack;
        rack.id = 4;
        rack.bypassed = true;
        rack.deltaSolo = true;
        ChainInfo chain;
        chain.id = 10;
        chain.elements.push_back(makeDeviceElement(makeEffect(7)));
        rack.chains.push_back(std::move(chain));

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        CHECK(countRole(plan, OpRole::RackDelta) == 0);
        REQUIRE(plan.diagnostics.size() == 1);
        CHECK(plan.diagnostics.front().find("delta solo on a bypassed rack measures nothing") !=
              std::string::npos);
    }

    SECTION("an analysis device has no difference to take, and says so") {
        // The delta button is DeviceType::Effect alone, so this flag cannot be
        // set from the UI at all. It can be stored, because the field is
        // serialised on every device, and a plan that quietly handed back the
        // chain would leave the project saying one thing and the engine doing
        // another with nothing in between to notice.
        auto analysis = makeEffect(7);
        analysis.deviceType = DeviceType::Analysis;
        analysis.deltaSolo = true;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(analysis));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        CHECK(countRole(plan, OpRole::DeviceDelta) == 0);
        REQUIRE(plan.diagnostics.size() == 1);
        CHECK(plan.diagnostics.front().find(
                  "delta solo on an analysis device has nothing to subtract") != std::string::npos);
    }

    SECTION("an analysis device without the flag is not reported at all") {
        auto analysis = makeEffect(7);
        analysis.deviceType = DeviceType::Analysis;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(analysis));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());
    }
}

TEST_CASE("A malformed output routing is reported", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].audioOutputDevice = "track:";

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    // The master fallback is the right behaviour; applying it silently is not.
    REQUIRE(plan.diagnostics.size() == 1);
    CHECK(plan.diagnostics.front().find("output routing") != std::string::npos);

    magda::engine::OpId masterInput = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == MASTER_TRACK_ID)
            masterInput = op;
    REQUIRE(masterInput != magda::engine::INVALID_OP_ID);
    CHECK(plan.ops[static_cast<std::size_t>(masterInput)].inputs.size() == 1);
}

TEST_CASE("A send to a track that no longer exists is reported as such",
          "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].sends.push_back(SendInfo{0, 1.0f, false, 99});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    // Emitting the tap anyway would leave it queued against a track that is
    // never compiled, and the end-of-run sweep would blame a routing cycle.
    CHECK(countRole(plan, OpRole::SendTap) == 0);
    REQUIRE(plan.diagnostics.size() == 1);
    CHECK(plan.diagnostics.front().find("does not exist") != std::string::npos);
    CHECK(plan.diagnostics.front().find("cycle") == std::string::npos);
}

TEST_CASE("Only MIDI consumers the compiler emits pull in a MIDI source",
          "[engine][plan][compiler]") {
    SECTION("a bypassed instrument does not") {
        auto instrument = makeInstrument(3);
        instrument.bypassed = true;

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(instrument));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::ClipMidi) == 0);
        CHECK(countRole(plan, OpRole::TrackMidiInput) == 0);
    }

    SECTION("an instrument behind chain power off does not") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.enabled = false;
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(3)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::ClipMidi) == 0);
    }

    SECTION("an instrument in a bypassed rack does not") {
        RackInfo rack;
        rack.id = 4;
        rack.bypassed = true;
        ChainInfo chain;
        chain.id = 10;
        chain.elements.push_back(makeDeviceElement(makeInstrument(3)));
        rack.chains.push_back(std::move(chain));

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::ClipMidi) == 0);
    }

    SECTION("but a MIDI sidechain reader still does, whatever the chain holds") {
        auto gate = makeEffect(7);
        gate.canReceiveMidi = true;
        gate.sidechain.type = SidechainConfig::Type::MIDI;
        gate.sidechain.sourceTrackId = 2;

        auto bypassed = makeInstrument(3);
        bypassed.bypassed = true;

        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(gate));
        tracks[1].chain.fxChainElements.push_back(makeDeviceElement(bypassed));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.diagnostics.empty());

        // Track 2's own instrument is bypassed, but track 1 reads its MIDI, so
        // its clips are compiled anyway. Track 1 has a MIDI consumer of its own
        // and gets a source for the ordinary reason.
        std::vector<magda::TrackId> clipMidiTracks;
        for (const auto op : opsWithRole(plan, OpRole::ClipMidi))
            clipMidiTracks.push_back(plan.ops[static_cast<std::size_t>(op)].key.trackId);
        CHECK(clipMidiTracks == std::vector<magda::TrackId>{2, 1});
    }
}

TEST_CASE("Sidechain discovery reaches every section emission does", "[engine][plan][compiler]") {
    // Mixer-analysis devices are rail-managed and none sets canSidechain today,
    // but emitDevice resolves a sidechain wherever it finds one, so collection
    // has to walk the same sections or ordering silently depends on luck.
    auto analysisWithSidechain = makeEffect(9);
    analysisWithSidechain.deviceType = DeviceType::Analysis;
    analysisWithSidechain.canSidechain = true;
    analysisWithSidechain.sidechain.type = SidechainConfig::Type::Audio;
    analysisWithSidechain.sidechain.sourceTrackId = 2;

    // Declared before its source, so only the ordering edge can resolve it.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.mixerAnalysisElements.push_back(PostFxChainElement{analysisWithSidechain});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    const auto device = opsWithRole(plan, OpRole::DeviceProcess).front();
    magda::engine::OpId sourceMeter = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMeter))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            sourceMeter = op;
    REQUIRE(sourceMeter != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, device, 2) == sourceMeter);
}

TEST_CASE("Auto input monitoring only counts while the track is armed",
          "[engine][plan][compiler]") {
    // Automatic monitoring passes input only while
    // armed. Emitting a live op for an unarmed Auto track would both add a op
    // the engine keeps silent and mark the whole chain downstream Live.
    SECTION("unarmed Auto emits no live input") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].inputMonitor = InputMonitorMode::Auto;
        tracks[0].audioInputDevice = "Input 1";
        tracks[0].midiInputDevice = "Keyboard";

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        CHECK(countRole(plan, OpRole::LiveAudioInput) == 0);
        CHECK(countRole(plan, OpRole::LiveMidiInput) == 0);
        for (const auto& op : plan.ops)
            CHECK(op.liveness == magda::engine::LivenessDomain::Deterministic);
    }

    SECTION("armed Auto emits it") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].inputMonitor = InputMonitorMode::Auto;
        tracks[0].recordArmed = true;
        tracks[0].audioInputDevice = "Input 1";

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::LiveAudioInput) == 1);
    }

    SECTION("In emits it without arming") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].inputMonitor = InputMonitorMode::In;
        tracks[0].audioInputDevice = "Input 1";

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(countRole(plan, OpRole::LiveAudioInput) == 1);
    }
}

TEST_CASE("A frozen track is reported as not compiled", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].frozen = true;
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    REQUIRE(plan.diagnostics.size() == 1);
    CHECK(plan.diagnostics.front().find("freeze") != std::string::npos);
}

TEST_CASE("validatePlan enforces the differ's identity precondition", "[engine][plan][validate]") {
    using magda::engine::LivenessDomain;
    using magda::engine::PlanOp;
    using magda::engine::PortRef;
    using magda::engine::SignalKind;

    RenderPlan plan;

    PlanOp source;
    source.kind = OpKind::ClipAudio;
    source.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio, 0};
    source.outputs = {SignalKind::Audio};
    plan.ops.push_back(source);

    // Same key, different op. The differ joins on the key, so this does not
    // fail loudly at swap time: it carries one op's state into the other.
    PlanOp collision;
    collision.kind = OpKind::Gain;
    collision.key = source.key;
    collision.inputs = {PortRef{0, 0}};
    collision.outputs = {SignalKind::Audio};
    plan.ops.push_back(collision);

    const auto problems = magda::engine::validatePlan(plan);
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().find("same key as op 0") != std::string::npos);
}

TEST_CASE("validatePlan enforces the delay invariants", "[engine][plan][validate]") {
    using magda::engine::PlanOp;
    using magda::engine::PortRef;
    using magda::engine::SignalKind;

    // Latency resolution reads a delay's sample count off the op it feeds, so
    // a delay on two edges, or behind another delay, is not a plan that is
    // merely odd: it is one whose compensation has no single answer.
    RenderPlan plan;

    PlanOp source;
    source.kind = OpKind::ClipAudio;
    source.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio, 0};
    source.outputs = {SignalKind::Audio};
    plan.ops.push_back(source);

    PlanOp delay;
    delay.kind = OpKind::Delay;
    delay.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::MixInputDelay, 0};
    delay.inputs = {PortRef{0, 0}};
    delay.outputs = {SignalKind::Audio};

    SECTION("a delay on two edges is rejected") {
        plan.ops.push_back(delay);

        for (int reader = 0; reader < 2; ++reader) {
            PlanOp mix;
            mix.kind = OpKind::MixAudio;
            mix.key = {1,
                       INVALID_RACK_ID,
                       INVALID_CHAIN_ID,
                       INVALID_DEVICE_ID,
                       OpRole::TrackAudioInput,
                       reader};
            mix.inputs = {PortRef{1, 0}};
            mix.outputs = {SignalKind::Audio};
            plan.ops.push_back(mix);
        }

        const auto problems = magda::engine::validatePlan(plan);
        REQUIRE(problems.size() == 1);
        CHECK(problems.front().find("read by 2 input slots") != std::string::npos);
    }

    SECTION("a delay behind another delay is rejected") {
        plan.ops.push_back(delay);

        auto stacked = delay;
        stacked.key.index = 1;
        stacked.inputs = {PortRef{1, 0}};
        plan.ops.push_back(stacked);

        PlanOp mix;
        mix.kind = OpKind::MixAudio;
        mix.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::TrackAudioInput,
                   0};
        mix.inputs = {PortRef{2, 0}};
        mix.outputs = {SignalKind::Audio};
        plan.ops.push_back(mix);

        const auto problems = magda::engine::validatePlan(plan);
        REQUIRE(problems.size() == 1);
        CHECK(problems.front().find("delay reading another delay") != std::string::npos);
    }

    SECTION("a delay keyed to nothing in particular is rejected") {
        delay.key.role = OpRole::TrackFader;
        plan.ops.push_back(delay);

        PlanOp mix;
        mix.kind = OpKind::MixAudio;
        mix.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::TrackAudioInput,
                   0};
        mix.inputs = {PortRef{1, 0}};
        mix.outputs = {SignalKind::Audio};
        plan.ops.push_back(mix);

        const auto problems = magda::engine::validatePlan(plan);
        REQUIRE(problems.size() == 1);
        CHECK(problems.front().find("not keyed to an input slot") != std::string::npos);
    }

    SECTION("an op wearing a delay's role without being one is rejected") {
        PlanOp gain;
        gain.kind = OpKind::Gain;
        gain.key = delay.key;
        gain.inputs = {PortRef{0, 0}};
        gain.outputs = {SignalKind::Audio};
        plan.ops.push_back(gain);

        const auto problems = magda::engine::validatePlan(plan);
        REQUIRE(problems.size() == 1);
        CHECK(problems.front().find("input-delay role but is not a delay") != std::string::npos);
    }
}

TEST_CASE("Latency compensation goes where paths can arrive apart",
          "[engine][plan][compiler][pdc]") {
    using magda::engine::OpKind;

    const auto delaysInto = [](const RenderPlan& plan, magda::engine::OpId consumer) {
        int delays = 0;
        for (std::size_t slot = 0;
             slot < plan.ops[static_cast<std::size_t>(consumer)].inputs.size(); ++slot) {
            const auto source = rawInputOp(plan, consumer, slot);
            if (source != magda::engine::INVALID_OP_ID &&
                plan.ops[static_cast<std::size_t>(source)].kind == OpKind::Delay)
                ++delays;
        }
        return delays;
    };

    SECTION("a sum of two, but not a sum of one") {
        // A track sums its own two sections, the arrangement and the session
        // (#2301), so a fan-in of two is what an ordinary track now is and the
        // delays go there like anywhere else. The master over a single track is
        // still the sum of one, and it still has none.
        std::vector<TrackInfo> tracks{makeTrack(1)};
        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto inputs = opsWithRole(plan, OpRole::TrackAudioInput);
        REQUIRE(inputs.size() == 2);
        CHECK(delaysInto(plan, inputs[0]) == 2);  // arrangement and session
        CHECK(delaysInto(plan, inputs[1]) == 0);  // the master, over one track
        CHECK(countRole(plan, OpRole::MixInputDelay) == 2);
    }

    SECTION("a device reading audio and MIDI, but not one reading audio alone") {
        // Track 2 has nothing in it that consumes MIDI, so no MIDI is compiled
        // for it at all and its effect has one input to wait on. Track 1's
        // effect takes the chain's MIDI as well as its audio, so it has two
        // paths to wait on. An instrument would have only the MIDI one, since
        // nothing is connected to its audio input.
        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        auto midiEffect = makeEffect(3);
        midiEffect.canReceiveMidi = true;
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(midiEffect));
        tracks[1].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(4)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto devices = opsWithRole(plan, OpRole::DeviceProcess);
        REQUIRE(devices.size() == 2);
        CHECK(delaysInto(plan, devices[0]) == 2);  // the MIDI effect: audio and MIDI
        CHECK(delaysInto(plan, devices[1]) == 0);  // the plain effect: audio alone
        CHECK(countRole(plan, OpRole::DeviceInputDelay) == 2);
    }

    SECTION("a device reading a sidechain") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        auto device = makeEffect(7);
        device.sidechain.type = SidechainConfig::Type::Audio;
        device.sidechain.sourceTrackId = 2;
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(device));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto devices = opsWithRole(plan, OpRole::DeviceProcess);
        REQUIRE(devices.size() == 1);
        CHECK(delaysInto(plan, devices[0]) == 2);  // audio and the key it is aligned with
    }

    SECTION("a rack's chains, and the MIDI they generate") {
        auto rack = std::make_unique<RackInfo>();
        rack->id = 5;
        for (const ChainId chainId : {ChainId{10}, ChainId{11}}) {
            ChainInfo chain;
            chain.id = chainId;
            auto arp = makeEffect(static_cast<DeviceId>(chainId));
            arp.deviceType = DeviceType::MIDI;
            arp.canReceiveMidi = true;
            arp.producesMidi = true;
            arp.midiInThru = false;
            chain.elements.push_back(makeDeviceElement(arp));
            rack->chains.push_back(std::move(chain));
        }

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(ChainElement{std::move(rack)});
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(9)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        const auto mix = opsWithRole(plan, OpRole::RackMix);
        REQUIRE(mix.size() == 1);
        CHECK(delaysInto(plan, mix.front()) == 2);

        const auto midiMix = opsWithRole(plan, OpRole::RackMidiMix);
        REQUIRE(midiMix.size() == 1);
        CHECK(delaysInto(plan, midiMix.front()) == 2);

        // Each chain's fader carries the chain's audio and its MIDI, which
        // arrive from different places and so may arrive apart.
        for (const auto fader : opsWithRole(plan, OpRole::RackChainFader))
            CHECK(delaysInto(plan, fader) == 2);
    }
}

TEST_CASE("validatePlan enforces liveness provenance", "[engine][plan][validate]") {
    using magda::engine::LivenessDomain;
    using magda::engine::PlanOp;
    using magda::engine::PortRef;
    using magda::engine::SignalKind;

    RenderPlan plan;

    PlanOp source;
    source.kind = OpKind::ClipAudio;
    source.key = {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio, 0};
    source.outputs = {SignalKind::Audio};
    plan.ops.push_back(source);

    SECTION("a live op with no live input and no live source is rejected") {
        PlanOp overTagged;
        overTagged.kind = OpKind::Gain;
        overTagged.key = {
            1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::TrackFader, 0};
        overTagged.liveness = LivenessDomain::Live;
        overTagged.inputs = {PortRef{0, 0}};
        overTagged.outputs = {SignalKind::Audio};
        plan.ops.push_back(overTagged);

        const auto problems = magda::engine::validatePlan(plan);
        REQUIRE(problems.size() == 1);
        CHECK(problems.front().find("is live but reads nothing live") != std::string::npos);
    }

    SECTION("an input source may be live on its own") {
        PlanOp input;
        input.kind = OpKind::AudioInput;
        input.key = {
            1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::LiveAudioInput, 0};
        input.liveness = LivenessDomain::Live;
        input.outputs = {SignalKind::Audio};
        plan.ops.push_back(input);

        CHECK(magda::engine::validatePlan(plan).empty());
    }
}

TEST_CASE("The post-FX stage sits on the side of the fader the chain says",
          "[engine][plan][compiler][postfx]") {
    // The routing contract behind #2087, asserted as reachability rather than as
    // op indices: what matters is which signal each stage reads, not where the
    // compiler happened to emit it.
    auto build = [](bool postFader) {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.postFxPostFader = postFader;
        tracks[0].chain.postFxChainElements.push_back(PostFxChainElement{makeEffect(8)});
        return magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
    };

    SECTION("post-fader by default: the fader feeds the post-FX device") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        CHECK(tracks[0].chain.postFxPostFader);
    }

    SECTION("post-fader: the device reads the fader, and the meter reads the device") {
        const auto plan = build(true);
        requireWellFormed(plan);

        // Track 1's fader is the first; the master has one too.
        const auto trackFader = opsWithRole(plan, OpRole::TrackFader).front();
        const auto meter = opsWithRole(plan, OpRole::TrackMeter).front();

        CHECK(deviceInput(plan, 8) == trackFader);
        CHECK(inputOp(plan, meter, 0) == deviceOutput(plan, 8));
    }

    SECTION("pre-fader: the device feeds the fader instead") {
        const auto plan = build(false);
        requireWellFormed(plan);

        const auto trackFader = opsWithRole(plan, OpRole::TrackFader).front();
        CHECK(inputOp(plan, trackFader, 0) == deviceOutput(plan, 8));
    }

    SECTION("post-fader: a post-fader send carries the post-FX processing") {
        // "Post-fader" has to mean after everything in the channel strip, or a
        // send and the track output disagree about what the track sounds like.
        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        tracks[0].chain.postFxChainElements.push_back(PostFxChainElement{makeEffect(8)});
        SendInfo send;
        send.busIndex = 0;
        send.preFader = false;
        send.destTrackId = 2;
        tracks[0].sends.push_back(send);

        const auto plan =
            magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
        requireWellFormed(plan);

        const auto taps = opsWithRole(plan, OpRole::SendTap);
        REQUIRE(taps.size() == 1);
        CHECK(inputOp(plan, taps.front(), 0) == deviceOutput(plan, 8));
    }

    SECTION("post-fader: a pre-fader send does not") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        tracks[0].chain.postFxChainElements.push_back(PostFxChainElement{makeEffect(8)});
        SendInfo send;
        send.busIndex = 0;
        send.preFader = true;
        send.destTrackId = 2;
        tracks[0].sends.push_back(send);

        const auto plan =
            magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
        requireWellFormed(plan);

        const auto taps = opsWithRole(plan, OpRole::SendTap);
        REQUIRE(taps.size() == 1);
        CHECK(inputOp(plan, taps.front(), 0) != deviceOutput(plan, 8));
    }

    SECTION("the master stays pre-fader whatever the flag says") {
        // Tracktion cannot represent a post-fader master stage:
        // createMasterPluginNode builds the whole of getMasterPluginList() and
        // only then wraps it in getMasterVolumePlugin(). Honouring the flag here
        // and not there would make the master the one place the engines
        // disagree, which is the failure this contract exists to prevent.
        auto master = makeMaster();
        master.chain.postFxPostFader = true;
        master.chain.postFxChainElements.push_back(PostFxChainElement{makeEffect(11)});

        std::vector<TrackInfo> tracks{makeTrack(1)};
        const auto plan = magda::engine::compileRenderPlan(tracks, master, withoutDeviceMeters());
        requireWellFormed(plan);

        // Two faders: track 1's, then the master's. The master's post-FX device
        // must feed the second, not read from it.
        const auto faders = opsWithRole(plan, OpRole::TrackFader);
        REQUIRE(faders.size() == 2);
        CHECK(inputOp(plan, faders.back(), 0) == deviceOutput(plan, 11));
    }

    SECTION("the mixer-analysis rail follows the post-FX stage") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        auto analysis = makeEffect(9);
        analysis.deviceType = DeviceType::Analysis;
        tracks[0].chain.mixerAnalysisElements.push_back(PostFxChainElement{analysis});

        const auto plan =
            magda::engine::compileRenderPlan(tracks, makeMaster(), withoutDeviceMeters());
        requireWellFormed(plan);

        const auto trackFader = opsWithRole(plan, OpRole::TrackFader).front();
        CHECK(deviceInput(plan, 9) == trackFader);
    }
}

TEST_CASE("Section-local device ids have distinct plan identities",
          "[engine][plan][compiler][device-identity]") {
    // TrackManager allocates one id space per section. The same integer in all
    // three sections is therefore an ordinary project, and every process op
    // must still have the unique key the differ requires.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(3)));
    tracks[0].chain.postFxChainElements.push_back(PostFxChainElement{makeEffect(3)});
    auto analysis = makeEffect(3);
    analysis.deviceType = DeviceType::Analysis;
    tracks[0].chain.mixerAnalysisElements.push_back(PostFxChainElement{analysis});

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    const auto problems = magda::engine::validatePlan(plan);

    INFO(magda::engine::dumpPlan(plan));
    REQUIRE(problems.empty());

    std::set<magda::engine::DeviceKey> devices;
    for (const auto& op : plan.ops)
        if (op.kind == OpKind::Device)
            devices.insert(op.key.deviceKey());

    CHECK(devices == std::set<magda::engine::DeviceKey>{
                         {ChainSegment::Fx, 3},
                         {ChainSegment::PostFx, 3},
                         {ChainSegment::MixerAnalysis, 3},
                     });
}

namespace {

/// An instrument with @p pairs stereo output pairs, the first of which is its
/// main output. Pins follow the current engine's layout, two channels per pair
/// from pin 1, which is what InstrumentRackManager reads a pair off.
DeviceInfo makeMultiOutInstrument(DeviceId id, int pairs) {
    auto device = makeInstrument(id);
    device.multiOut.isMultiOut = true;
    device.multiOut.totalOutputChannels = pairs * 2;
    for (int pair = 0; pair < pairs; ++pair) {
        MultiOutOutputPair out;
        out.outputIndex = pair;
        out.name = "St." + juce::String(pair * 2 + 1) + "-" + juce::String(pair * 2 + 2);
        out.firstPin = 1 + pair * 2;
        out.numChannels = 2;
        device.multiOut.outputPairs.push_back(out);
    }
    return device;
}

/// A MultiOut track reading @p pair of @p deviceId on @p sourceTrack.
TrackInfo makeMultiOutTrack(TrackId id, TrackId sourceTrack, DeviceId deviceId, int pair) {
    auto track = makeTrack(id, TrackType::MultiOut);
    track.multiOutLink = MultiOutTrackLink{sourceTrack, deviceId, pair};
    return track;
}

/// The chain-head mix of one track.
magda::engine::OpId trackInput(const RenderPlan& plan, TrackId trackId) {
    for (const auto op : opsWithRole(plan, OpRole::TrackAudioInput))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == trackId)
            return op;
    return magda::engine::INVALID_OP_ID;
}

/// The device's process op, whatever else it emits.
magda::engine::OpId deviceProcess(const RenderPlan& plan, DeviceId deviceId) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (key.deviceId == deviceId && key.role == OpRole::DeviceProcess)
            return static_cast<magda::engine::OpId>(i);
    }
    return magda::engine::INVALID_OP_ID;
}

bool anyDiagnosticContains(const RenderPlan& plan, const std::string& text) {
    return std::ranges::any_of(plan.diagnostics, [&text](const std::string& message) {
        return message.find(text) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("A multi-out pair feeds the track that opened it", "[engine][plan][compiler]") {
    // The reading track is declared before its source, so only the multi-out
    // dependency can put the source first.
    std::vector<TrackInfo> tracks{makeMultiOutTrack(2, 1, 7, 1), makeTrack(1)};
    tracks[1].chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 3)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    const auto process = deviceProcess(plan, 7);
    REQUIRE(process != magda::engine::INVALID_OP_ID);

    // Three pairs: the main output at port 0, and one port each for pairs 1
    // and 2, whether or not a track opened them.
    const auto& outputs = plan.ops[static_cast<std::size_t>(process)].outputs;
    REQUIRE(outputs.size() == 3);
    CHECK(std::ranges::all_of(outputs, [](magda::engine::PortDesc port) {
        return port.kind == magda::engine::SignalKind::Audio;
    }));

    // The reading track's chain head is that pair's port and nothing else.
    const auto input = trackInput(plan, 2);
    REQUIRE(input != magda::engine::INVALID_OP_ID);
    const auto& inputs = plan.ops[static_cast<std::size_t>(input)].inputs;
    REQUIRE(inputs.size() == 1);
    CHECK(rawInputOp(plan, input, 0) == process);
    CHECK(inputs[0].port == 1);
}

TEST_CASE("A multi-out pair carries the width the device gave it", "[engine][plan][compiler]") {
    // A drum machine with one mono pair among its stereo ones.
    auto drums = makeMultiOutInstrument(7, 3);
    drums.multiOut.outputPairs[2].numChannels = 1;

    std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(drums));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    // Pair p is port p, and pair 0 is the main output. Only the pair the
    // device called mono is narrow.
    const auto& outputs = plan.ops[static_cast<std::size_t>(deviceProcess(plan, 7))].outputs;
    REQUIRE(outputs.size() == 3);
    CHECK(outputs[0].channels == 2);
    CHECK(outputs[1].channels == 2);
    CHECK(outputs[2].channels == 1);
}

TEST_CASE("A multi-out link the source device cannot honour is reported",
          "[engine][plan][compiler]") {
    SECTION("a pair the device does not have") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 4)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 2)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(anyDiagnosticContains(plan, "is not a pair that device has"));
        CHECK(plan.ops[static_cast<std::size_t>(trackInput(plan, 2))].inputs.empty());
    }

    SECTION("a device that is not multi-out at all") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(7)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(anyDiagnosticContains(plan, "is not a pair that device has"));
    }

    SECTION("the main pair, which stays on the source track") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 0)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 3)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(anyDiagnosticContains(plan, "is not a pair this track can read"));
    }

    SECTION("two tracks on one pair") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1),
                                      makeMultiOutTrack(3, 1, 7, 1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 2)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(anyDiagnosticContains(plan, "is already read by track 2"));

        // The first claim stands; the second track reads nothing.
        CHECK(plan.ops[static_cast<std::size_t>(trackInput(plan, 2))].inputs.size() == 1);
        CHECK(plan.ops[static_cast<std::size_t>(trackInput(plan, 3))].inputs.empty());
    }
}

TEST_CASE("A bypassed multi-out instrument silences its pairs without reporting them",
          "[engine][plan][compiler]") {
    auto instrument = makeMultiOutInstrument(7, 3);
    instrument.bypassed = true;

    std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(instrument));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    // A bypassed device is not in the plan, so the pair has nothing to read;
    // the link is still the one the model meant, so nothing is reported.
    CHECK(plan.diagnostics.empty());
    CHECK(deviceProcess(plan, 7) == magda::engine::INVALID_OP_ID);
    CHECK(plan.ops[static_cast<std::size_t>(trackInput(plan, 2))].inputs.empty());
}

TEST_CASE("A multi-out instrument that also writes MIDI keeps its pairs after it",
          "[engine][plan][compiler]") {
    auto instrument = makeMultiOutInstrument(7, 2);
    instrument.producesMidi = true;

    std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(instrument));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);
    CHECK(plan.diagnostics.empty());

    const auto process = deviceProcess(plan, 7);
    const auto& outputs = plan.ops[static_cast<std::size_t>(process)].outputs;
    REQUIRE(outputs.size() == 3);
    CHECK(outputs[0] == magda::engine::SignalKind::Audio);
    CHECK(outputs[1] == magda::engine::SignalKind::Midi);
    CHECK(outputs[2] == magda::engine::SignalKind::Audio);

    const auto input = trackInput(plan, 2);
    CHECK(plan.ops[static_cast<std::size_t>(input)].inputs[0].port == 2);
}

TEST_CASE("A rack chain routed to an aux output reaches nothing and says so",
          "[engine][plan][compiler]") {
    RackInfo rack;
    rack.id = 4;
    ChainInfo mainChain;
    mainChain.id = 10;
    mainChain.elements.push_back(makeDeviceElement(makeEffect(7)));
    ChainInfo auxChain;
    auxChain.id = 11;
    auxChain.outputIndex = 1;
    auxChain.elements.push_back(makeDeviceElement(makeEffect(8)));
    rack.chains.push_back(std::move(mainChain));
    rack.chains.push_back(std::move(auxChain));

    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK(anyDiagnosticContains(plan, "reaches nothing, the chain is silent"));

    // Only the main chain is compiled, and the device on the aux chain with it.
    CHECK(countRole(plan, OpRole::RackChainFader) == 1);
    CHECK(deviceProcess(plan, 8) == magda::engine::INVALID_OP_ID);
}

TEST_CASE("A multi-out link survives its source not being compiled", "[engine][plan][compiler]") {
    // Whether the link is sound and whether the device is in the plan are two
    // questions. Everything that leaves an instrument out of the plan would
    // otherwise read as a broken link, and the track would be told its pair
    // does not exist when the only thing that happened is that the chain it
    // sits on is not running. Bypass is the other way in, and has its own case
    // above.
    SECTION("the source track's insert chain is powered off") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1)};
        tracks[0].chain.enabled = false;
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 3)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        CHECK(plan.diagnostics.empty());
        CHECK(deviceProcess(plan, 7) == magda::engine::INVALID_OP_ID);
        CHECK(plan.ops[static_cast<std::size_t>(trackInput(plan, 2))].inputs.empty());
    }

    SECTION("but a link to a track that does not exist is still reported") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 9, 7, 1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 3)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(anyDiagnosticContains(plan, "is not a pair that device has"));
    }
}

TEST_CASE("A multi-out pair that carries nothing is not an ordering dependency",
          "[engine][plan][compiler]") {
    // The edge only exists where a connection does. An edge for a pair nothing
    // carries can invent a cycle, and the breaker resolves a cycle by dropping
    // a connection: a real one would go so an imaginary one could be ordered.
    auto instrument = makeMultiOutInstrument(7, 3);
    instrument.bypassed = true;

    // Track 2 reads a pair of the bypassed instrument on track 1, and track 1
    // takes track 2's output as its own input. Only the multi-out edge can
    // close that loop, and it must not.
    std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(instrument));
    tracks[1].audioOutputDevice = "track:1";

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    CHECK_FALSE(anyDiagnosticContains(plan, "routing cycle"));
    CHECK_FALSE(anyDiagnosticContains(plan, "arrived after it was compiled"));

    // Track 2's real connection into track 1 survives, which is what a dropped
    // edge would have cost: track 1's own two sections, and track 2's output.
    const auto input = trackInput(plan, 1);
    REQUIRE(input != magda::engine::INVALID_OP_ID);
    REQUIRE(plan.ops[static_cast<std::size_t>(input)].inputs.size() == 3);

    magda::engine::OpId readerMute = magda::engine::INVALID_OP_ID;
    for (const auto op : opsWithRole(plan, OpRole::TrackMute))
        if (plan.ops[static_cast<std::size_t>(op)].key.trackId == 2)
            readerMute = op;
    REQUIRE(readerMute != magda::engine::INVALID_OP_ID);
    CHECK(inputOp(plan, input, 2) == readerMute);
}

TEST_CASE("A device with more pairs than one op can carry says so", "[engine][plan][compiler]") {
    const auto overBudget = magda::engine::kMaxMultiOutPairs + 2;

    SECTION("the device reports the pairs it could not compile") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(
            makeDeviceElement(makeMultiOutInstrument(7, overBudget + 1)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        // Never a silent shortening: the ports stop at the budget and the drop
        // is named.
        CHECK(anyDiagnosticContains(plan, "one device can carry, the rest are not compiled"));
        const auto process = deviceProcess(plan, 7);
        CHECK(plan.ops[static_cast<std::size_t>(process)].outputs.size() ==
              static_cast<std::size_t>(magda::engine::kMaxMultiOutPairs) + 1);
    }

    SECTION("a track reading past the budget is told which limit it hit") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, overBudget)};
        tracks[0].chain.fxChainElements.push_back(
            makeDeviceElement(makeMultiOutInstrument(7, overBudget + 1)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);

        // The device has that pair, so this is not the broken-link message.
        CHECK(anyDiagnosticContains(plan, "pairs one device can carry"));
        CHECK_FALSE(anyDiagnosticContains(plan, "is not a pair that device has"));
    }

    SECTION("a pair inside the budget still routes on the same device") {
        std::vector<TrackInfo> tracks{makeTrack(1), makeMultiOutTrack(2, 1, 7, 1)};
        tracks[0].chain.fxChainElements.push_back(
            makeDeviceElement(makeMultiOutInstrument(7, overBudget + 1)));

        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        requireWellFormed(plan);
        CHECK(plan.ops[static_cast<std::size_t>(trackInput(plan, 2))].inputs.size() == 1);
    }
}
