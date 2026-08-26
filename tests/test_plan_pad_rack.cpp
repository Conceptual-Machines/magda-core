#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "core/DrumGridPads.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
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
