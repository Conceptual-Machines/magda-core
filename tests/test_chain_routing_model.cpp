#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ChainRoutingModel.hpp"

namespace {

magda::DeviceInfo makeDevice(magda::DeviceId id, bool instrument = false) {
    magda::DeviceInfo device;
    device.id = id;
    device.isInstrument = instrument;
    device.deviceType = instrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    return device;
}

}  // namespace

TEST_CASE("ChainRoutingModel models generic MIDI source and thru modes", "[chain][routing]") {
    auto instrument = makeDevice(10, true);
    auto midiFx = makeDevice(11);
    midiFx.canReceiveMidi = true;
    midiFx.producesMidi = true;
    midiFx.midiInThru = false;

    auto mergingMidiFx = makeDevice(14);
    mergingMidiFx.canReceiveMidi = true;
    mergingMidiFx.producesMidi = true;
    mergingMidiFx.midiInThru = true;

    auto sidechainedFx = makeDevice(12);
    sidechainedFx.canReceiveMidi = true;
    sidechainedFx.sidechain.type = magda::SidechainConfig::Type::MIDI;
    sidechainedFx.sidechain.sourceTrackId = 99;

    auto downstreamFx = makeDevice(13);
    downstreamFx.canReceiveMidi = true;

    std::vector<magda::ChainElement> elements;
    elements.push_back(magda::makeDeviceElement(instrument));
    elements.push_back(magda::makeDeviceElement(midiFx));
    elements.push_back(magda::makeDeviceElement(mergingMidiFx));
    elements.push_back(magda::makeDeviceElement(sidechainedFx));
    elements.push_back(magda::makeDeviceElement(downstreamFx));

    const auto plan = magda::routing::compileTrackChainRouting(1, elements);

    REQUIRE(plan.containerKind == magda::routing::ChainContainerKind::Track);
    REQUIRE(plan.trackId == 1);
    REQUIRE(plan.nodes.size() == 5);

    CHECK(plan.nodes[0].deviceId == 10);
    CHECK(plan.nodes[0].receivesChainMidi());
    CHECK_FALSE(plan.nodes[0].replacesChainMidi());
    CHECK(plan.nodes[0].passesRawMidiInput());
    CHECK_FALSE(plan.nodes[0].outputsPluginMidi());
    CHECK(plan.nodes[0].injectsAudio());

    CHECK(plan.nodes[1].deviceId == 11);
    CHECK(plan.nodes[1].receivesChainMidi());
    CHECK(plan.nodes[1].replacesChainMidi());
    CHECK_FALSE(plan.nodes[1].passesRawMidiInput());
    CHECK(plan.nodes[1].outputsPluginMidi());

    CHECK(plan.nodes[2].deviceId == 14);
    CHECK(plan.nodes[2].receivesChainMidi());
    CHECK_FALSE(plan.nodes[2].replacesChainMidi());
    CHECK(plan.nodes[2].passesRawMidiInput());
    CHECK(plan.nodes[2].outputsPluginMidi());

    CHECK(plan.nodes[3].deviceId == 12);
    CHECK_FALSE(plan.nodes[3].receivesChainMidi());
    CHECK(plan.nodes[3].usesExternalMidiSidechain());
    CHECK(plan.nodes[3].midiSidechainSourceTrackId == 99);
    CHECK_FALSE(plan.nodes[3].replacesChainMidi());
    CHECK_FALSE(plan.nodes[3].passesRawMidiInput());
    CHECK_FALSE(plan.nodes[3].outputsPluginMidi());

    CHECK(plan.nodes[4].deviceId == 13);
    CHECK(plan.nodes[4].receivesChainMidi());
    CHECK_FALSE(plan.nodes[4].replacesChainMidi());
    CHECK(plan.nodes[4].passesRawMidiInput());
    CHECK_FALSE(plan.nodes[4].outputsPluginMidi());
}

TEST_CASE("ChainRoutingModel compiles the same MIDI policy for rack chains", "[chain][routing]") {
    magda::ChainInfo chain;
    chain.id = 200;

    auto instrument = makeDevice(20, true);
    auto sidechainedFx = makeDevice(21);
    sidechainedFx.canReceiveMidi = true;
    sidechainedFx.sidechain.type = magda::SidechainConfig::Type::MIDI;
    sidechainedFx.sidechain.sourceTrackId = 7;

    magda::RackInfo nestedRack;
    nestedRack.id = 22;

    chain.elements.push_back(magda::makeDeviceElement(instrument));
    chain.elements.push_back(magda::makeDeviceElement(sidechainedFx));
    chain.elements.push_back(magda::makeRackElement(std::move(nestedRack)));

    const auto plan = magda::routing::compileRackChainRouting(3, 100, chain);

    REQUIRE(plan.containerKind == magda::routing::ChainContainerKind::Rack);
    REQUIRE(plan.trackId == 3);
    REQUIRE(plan.rackId == 100);
    REQUIRE(plan.chainId == 200);
    REQUIRE(plan.nodes.size() == 3);

    CHECK(plan.nodes[0].deviceId == 20);
    CHECK(plan.nodes[0].receivesChainMidi());
    CHECK_FALSE(plan.nodes[0].replacesChainMidi());
    CHECK(plan.nodes[0].injectsAudio());

    CHECK(plan.nodes[1].deviceId == 21);
    CHECK_FALSE(plan.nodes[1].receivesChainMidi());
    CHECK(plan.nodes[1].usesExternalMidiSidechain());
    CHECK(plan.nodes[1].midiSidechainSourceTrackId == 7);

    CHECK(plan.nodes[2].kind == magda::routing::ChainRoutingNodeKind::Rack);
    CHECK(plan.nodes[2].rackId == 22);
    CHECK(plan.nodes[2].receivesChainMidi());
    CHECK(plan.nodes[2].replacesChainMidi());
}
