#include <algorithm>
#include <catch2/catch_test_macros.hpp>
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

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Audio) {
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
    return device;
}

DeviceInfo makeInstrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Instrument " + juce::String(id);
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
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

/// The op an input slot reads, or -1 when the slot is unconnected.
magda::engine::OpId inputOp(const RenderPlan& plan, magda::engine::OpId op, std::size_t slot) {
    const auto& inputs = plan.ops[static_cast<std::size_t>(op)].inputs;
    if (slot >= inputs.size())
        return magda::engine::INVALID_OP_ID;
    return inputs[slot].op;
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
ops=13 outputs=1
[  0] ClipAudio   det   T1:clipAudio                   in=-                out=audio       deps=0
[  1] MixAudio    det   T1:trackAudioInput             in=0:0              out=audio       deps=1
[  2] Device      det   T1/D7:deviceProcess            in=1:0,-,-          out=audio       deps=1
[  3] Gain        det   T1/D7:deviceGain               in=2:0              out=audio       deps=1
[  4] Meter       det   T1/D7:deviceMeter              in=3:0              out=audio       deps=1
[  5] Fader       det   T1:trackFader                  in=4:0              out=audio       deps=1
[  6] Meter       det   T1:trackMeter                  in=5:0              out=audio       deps=1
[  7] Gain        det   T1:trackMute                   in=6:0              out=audio       deps=1
[  8] MixAudio    det   T-2:trackAudioInput            in=7:0              out=audio       deps=1
[  9] Fader       det   T-2:trackFader                 in=8:0              out=audio       deps=1
[ 10] Meter       det   T-2:trackMeter                 in=9:0              out=audio       deps=1
[ 11] Gain        det   T-2:trackMute                  in=10:0             out=audio       deps=1
[ 12] Output      det   T-2:hardwareOutput             in=11:0             out=-           deps=1
ready=0
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

TEST_CASE("The chord track is excluded from the plan", "[engine][plan][compiler]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(9, TrackType::Chord)};

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    for (const auto& op : plan.ops)
        CHECK(op.key.trackId != 9);
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

    // Own clips first, then the routed source.
    REQUIRE(plan.ops[static_cast<std::size_t>(destMidi)].inputs.size() == 2);
    CHECK(inputOp(plan, destMidi, 1) == sourceMidi);
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

    REQUIRE(plan.ops[static_cast<std::size_t>(destInput)].inputs.size() == 2);
    CHECK(inputOp(plan, destInput, 1) == sourceMute);
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

TEST_CASE("A rack-level sidechain is reported as unsupported", "[engine][plan][compiler]") {
    RackInfo rack;
    rack.id = 4;
    rack.sidechain.type = SidechainConfig::Type::MIDI;
    rack.sidechain.sourceTrackId = 2;
    ChainInfo chain;
    chain.id = 10;
    chain.elements.push_back(makeDeviceElement(makeEffect(7)));
    rack.chains.push_back(std::move(chain));

    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    requireWellFormed(plan);

    // It drives rack modulation rather than signal, so the plan carries no edge
    // for it. That is a gap the contract says to name rather than pass over.
    REQUIRE(plan.diagnostics.size() == 1);
    CHECK(plan.diagnostics.front().find("rack 4") != std::string::npos);
    CHECK(plan.diagnostics.front().find("modulation") != std::string::npos);
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
