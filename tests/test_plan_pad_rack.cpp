#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "core/DrumGridPads.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

// Compiling a pad-per-chain device (#2200). The device never becomes an op:
// its pads are instrument chains, each gated to the notes its range claims.

using namespace magda;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::RenderPlan;

namespace {

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makePadInstrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Sampler " + juce::String(id);
    device.pluginId = "magdasampler";
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    device.format = PluginFormat::Internal;
    device.audioOutputChannels = 2;
    device.parameters.push_back(ParameterInfo(0, "Level", "", 0.0f, 1.0f, 0.5f));
    return device;
}

ChainInfo makePad(ChainId id, int lowNote, int highNote, int rootNote, DeviceId deviceId) {
    ChainInfo pad;
    pad.id = id;
    pad.name = "Pad " + juce::String(id);
    pad.lowNote = lowNote;
    pad.highNote = highNote;
    pad.rootNote = rootNote;
    pad.elements.push_back(makePadInstrument(deviceId));
    return pad;
}

/// A Drum Grid with two pads, as the projection produces one.
DeviceInfo makeDrumGrid(DeviceId id, std::vector<ChainInfo> pads) {
    DeviceInfo drumGrid;
    drumGrid.id = id;
    drumGrid.name = "Drum Grid";
    drumGrid.pluginId = "drumgrid";
    drumGrid.isInstrument = true;
    drumGrid.deviceType = DeviceType::Instrument;
    drumGrid.format = PluginFormat::Internal;

    auto rack = std::make_unique<RackInfo>();
    rack->id = padRackIdFor(id);
    rack->chains = std::move(pads);
    drumGrid.padRack.reset(std::move(rack));

    return drumGrid;
}

RenderPlan compileWith(DeviceInfo drumGrid) {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(std::move(drumGrid));
    return magda::engine::compileRenderPlan({track}, makeMaster(), {});
}

std::vector<const magda::engine::PlanOp*> opsOfKind(const RenderPlan& plan, OpKind kind) {
    std::vector<const magda::engine::PlanOp*> found;
    for (const auto& op : plan.ops)
        if (op.kind == kind)
            found.push_back(&op);
    return found;
}

}  // namespace

TEST_CASE("A pad rack compiles to a well formed plan", "[engine][plan][padrack]") {
    const auto plan =
        compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), makePad(1, 38, 40, 60, 102)}));

    const auto problems = magda::engine::validatePlan(plan);
    for (const auto& problem : problems)
        UNSCOPED_INFO("validate: " << problem);
    CHECK(problems.empty());

    for (const auto& diagnostic : plan.diagnostics)
        UNSCOPED_INFO("diagnostic: " << diagnostic);
}

TEST_CASE("Each pad gets its own note gate", "[engine][plan][padrack]") {
    const auto plan =
        compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), makePad(1, 38, 40, 60, 102)}));

    const auto gates = opsOfKind(plan, OpKind::MidiNoteGate);
    REQUIRE(gates.size() == 2);

    CHECK(gates[0]->noteGateLow == 36);
    CHECK(gates[0]->noteGateHigh == 36);
    CHECK(gates[0]->noteGateTranspose == 24);

    CHECK(gates[1]->noteGateLow == 38);
    CHECK(gates[1]->noteGateHigh == 40);
    CHECK(gates[1]->noteGateTranspose == 22);

    // Keyed apart, or the differ carries one pad into the other.
    CHECK_FALSE(gates[0]->key == gates[1]->key);
}

TEST_CASE("A Drum Grid itself is never a device op", "[engine][plan][padrack]") {
    const auto plan =
        compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), makePad(1, 38, 40, 60, 102)}));

    for (const auto* op : opsOfKind(plan, OpKind::Device))
        CHECK(op->key.deviceId != 10);

    // The pads' own devices are what run.
    std::vector<DeviceId> deviceIds;
    for (const auto* op : opsOfKind(plan, OpKind::Device))
        deviceIds.push_back(op->key.deviceId);
    CHECK(std::find(deviceIds.begin(), deviceIds.end(), 101) != deviceIds.end());
    CHECK(std::find(deviceIds.begin(), deviceIds.end(), 102) != deviceIds.end());
}

TEST_CASE("A pad that answers to every note needs no gate", "[engine][plan][padrack]") {
    auto pad = makePad(0, 0, -1, 0, 101);
    REQUIRE(pad.answersToEveryNote());

    const auto plan = compileWith(makeDrumGrid(10, {std::move(pad)}));
    CHECK(opsOfKind(plan, OpKind::MidiNoteGate).empty());
}

TEST_CASE("A bypassed Drum Grid passes the chain through", "[engine][plan][padrack]") {
    auto drumGrid = makeDrumGrid(10, {makePad(0, 36, 36, 60, 101)});
    drumGrid.bypassed = true;

    const auto plan = compileWith(std::move(drumGrid));
    CHECK(opsOfKind(plan, OpKind::MidiNoteGate).empty());
    for (const auto* op : opsOfKind(plan, OpKind::Device))
        CHECK(op->key.deviceId != 101);
}

TEST_CASE("A bypassed pad is left out of the mix", "[engine][plan][padrack]") {
    auto quiet = makePad(1, 38, 40, 60, 102);
    quiet.bypassed = true;

    const auto plan =
        compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), std::move(quiet)}));

    REQUIRE(opsOfKind(plan, OpKind::MidiNoteGate).size() == 1);
    for (const auto* op : opsOfKind(plan, OpKind::Device))
        CHECK(op->key.deviceId != 102);
}

TEST_CASE("A Drum Grid inside a rack compiles", "[engine][plan][padrack]") {
    // 1.0.0-drumgrid-rack.mgd's shape: the pad rack is nested in a user rack, so
    // the pad ops carry the pad rack's id while the chain around them carries
    // the outer rack's.
    auto chain = ChainInfo{};
    chain.id = 1;
    chain.elements.push_back(
        makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), makePad(1, 38, 40, 60, 102)}));

    auto rack = std::make_unique<RackInfo>();
    rack->id = 1;
    rack->chains.push_back(std::move(chain));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(std::move(rack));

    const auto plan = magda::engine::compileRenderPlan({track}, makeMaster(), {});

    const auto problems = magda::engine::validatePlan(plan);
    for (const auto& problem : problems)
        UNSCOPED_INFO("validate: " << problem);
    CHECK(problems.empty());

    CHECK(opsOfKind(plan, OpKind::MidiNoteGate).size() == 2);
}

TEST_CASE("A pad device with no id is reported, not compiled to a broken plan",
          "[engine][plan][padrack]") {
    // Both corpus projects are old enough that their pad plugins carry no
    // DeviceId: a Drum Grid allocates one when it restores a plugin, so they
    // arrive on capture and not at load. Two such devices in one pad would key
    // the same op, and a malformed plan costs the whole project rather than the
    // pad.
    auto pad = makePad(0, 36, 36, 60, INVALID_DEVICE_ID);
    auto effect = makePadInstrument(INVALID_DEVICE_ID);
    effect.isInstrument = false;
    effect.deviceType = DeviceType::Effect;
    pad.elements.push_back(std::move(effect));

    const auto plan = compileWith(makeDrumGrid(10, {std::move(pad)}));

    const auto problems = magda::engine::validatePlan(plan);
    for (const auto& problem : problems)
        UNSCOPED_INFO("validate: " << problem);
    CHECK(problems.empty());

    const auto said = std::ranges::any_of(plan.diagnostics, [](const std::string& diagnostic) {
        return diagnostic.find("no device id") != std::string::npos;
    });
    CHECK(said);
}

TEST_CASE("A pad's mix keeps the Drum Grid's slot gain and meter", "[engine][plan][padrack]") {
    // An expanded Drum Grid is still a device in the chain: what it made has to
    // pass its slot's gain and meter before it reaches the bus, the way every
    // other instrument's output does.
    const auto plan = compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101)}));

    const auto gains = opsOfKind(plan, OpKind::Gain);
    const auto slotGain = std::ranges::find_if(gains, [](const magda::engine::PlanOp* op) {
        return op->key.role == OpRole::DeviceGain && op->key.deviceId == 10;
    });
    REQUIRE(slotGain != gains.end());

    const auto meters = opsOfKind(plan, OpKind::Meter);
    CHECK(std::ranges::any_of(meters, [](const magda::engine::PlanOp* op) {
        return op->key.role == OpRole::DeviceMeter && op->key.deviceId == 10;
    }));
}

TEST_CASE("Pads on an unclaimed bus are reported, not folded into the main mix",
          "[engine][plan][padrack]") {
    // The model says these pads play somewhere else. Mixing them into the
    // device's own output would render a project nobody saved.
    auto aux = makePad(1, 38, 40, 60, 102);
    aux.outputIndex = 2;

    const auto plan = compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), std::move(aux)}));

    CHECK(std::ranges::any_of(plan.diagnostics, [](const std::string& diagnostic) {
        return diagnostic.find("bus 2 reaches no track") != std::string::npos;
    }));

    // The main mix takes one pad, not both.
    const auto mixes = opsOfKind(plan, OpKind::MixAudio);
    const auto mainMix = std::ranges::find_if(mixes, [](const magda::engine::PlanOp* op) {
        return op->key.role == OpRole::RackMix && op->key.index == 0;
    });
    if (mainMix != mixes.end())
        CHECK((*mainMix)->inputs.size() == 1);
}

TEST_CASE("A pad's devices reach the parameter table", "[engine][plan][padrack]") {
    // Without the pad-rack walk, a pad device's parameters, macros and mods have
    // no address and nothing consumes them.
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(
        makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), makePad(1, 38, 40, 60, 102)}));

    const auto plan = magda::engine::compileRenderPlan({track}, makeMaster(), {});
    const auto table = magda::engine::compileParamTable(plan, {track}, makeMaster());

    const auto addressed = [&](DeviceId deviceId) {
        return std::ranges::any_of(table.keys, [deviceId](const magda::engine::ParamKey& key) {
            return key.device.deviceId == deviceId;
        });
    };
    CHECK(addressed(101));
    CHECK(addressed(102));
}

TEST_CASE("A bypassed Drum Grid keeps its pad devices' runtimes", "[engine][plan][padrack]") {
    // Bypass takes the pad Device ops out of the plan. If the model id set does
    // not name the pad devices either, their runtimes are released and
    // re-enabling rebuilds the plugins, losing their tails and state.
    auto drumGrid = makeDrumGrid(10, {makePad(0, 36, 36, 60, 101), makePad(1, 38, 40, 60, 102)});
    drumGrid.bypassed = true;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(std::move(drumGrid));

    const auto ids = magda::engine::collectRuntimeStateIds({track}, makeMaster());

    const auto named = [&](DeviceId deviceId) {
        return ids.devices.contains(magda::engine::DeviceKey{ChainSegment::Fx, deviceId});
    };
    CHECK(named(10));
    CHECK(named(101));
    CHECK(named(102));
}

TEST_CASE("A pad fader binds by the pad's slot, not by its chain id", "[engine][plan][padrack]") {
    // A Drum Grid reaches padLevelN and padPanN by the pad's bottom note, not by
    // the order its chains were made, so the two differ whenever pads were not
    // added in note order. Binding by chain id would have this pad, which sits
    // at slot 13, reading slot 0's level.
    //
    // Pad 0 here is chain id 0 at note 24, which is slot 0 -- the case where the
    // two agree, and the one that hid this.
    auto drumGrid = makeDrumGrid(10, {makePad(0, 24, 24, 60, 101), makePad(1, 37, 37, 60, 102)});

    const auto padParam = [](int index, const char* stableId, float min, float max) {
        ParameterInfo param(index, stableId, "", min, max, 0.0f);
        param.stableId = stableId;
        return param;
    };
    // The device declares a fixed pair per pad, in slot order.
    for (int slot = 0; slot < 20; ++slot) {
        drumGrid.parameters.push_back(
            padParam(slot * 2, ("padLevel" + std::to_string(slot)).c_str(), -60.0f, 12.0f));
        drumGrid.parameters.push_back(
            padParam(slot * 2 + 1, ("padPan" + std::to_string(slot)).c_str(), -1.0f, 1.0f));
    }

    const auto plan = compileWith(std::move(drumGrid));

    std::vector<const magda::engine::PlanOp*> padFaders;
    for (const auto* op : opsOfKind(plan, OpKind::Fader))
        if (op->key.role == OpRole::RackChainFader)
            padFaders.push_back(op);

    REQUIRE(padFaders.size() == 2);
    CHECK(padFaders[0]->key.deviceId == 10);

    // Note 24 is slot 0: parameters 0 and 1.
    CHECK(padFaders[0]->padLevelParam == 0);
    CHECK(padFaders[0]->padPanParam == 1);

    // Note 37 is slot 13, whatever its chain id: parameters 26 and 27. Chain id
    // 1 would have read 2 and 3.
    CHECK(padFaders[1]->padLevelParam == 26);
    CHECK(padFaders[1]->padPanParam == 27);
}

TEST_CASE("A pad whose range starts below the grid binds nothing", "[engine][plan][padrack]") {
    // No slot to read, so the fader keeps the published value rather than
    // reading whichever parameter a negative index landed on.
    auto drumGrid = makeDrumGrid(10, {makePad(0, 12, 12, 60, 101)});
    ParameterInfo level(0, "padLevel0", "", -60.0f, 12.0f, 0.0f);
    level.stableId = "padLevel0";
    drumGrid.parameters.push_back(level);

    const auto plan = compileWith(std::move(drumGrid));

    for (const auto* op : opsOfKind(plan, OpKind::Fader))
        if (op->key.role == OpRole::RackChainFader)
            CHECK(op->padLevelParam == -1);
}

TEST_CASE("A pad fader on a device with no pad parameters binds nothing",
          "[engine][plan][padrack]") {
    // Resolved by stable id, so a device that declares none simply keeps the
    // published value rather than reading whatever sits at that index.
    const auto plan = compileWith(makeDrumGrid(10, {makePad(0, 36, 36, 60, 101)}));

    for (const auto* op : opsOfKind(plan, OpKind::Fader))
        if (op->key.role == OpRole::RackChainFader) {
            CHECK(op->padLevelParam == -1);
            CHECK(op->padPanParam == -1);
        }
}
