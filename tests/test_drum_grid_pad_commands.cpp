#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/MacroInfo.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

// Editing a Drum Grid's pads through the model (#2207).
//
// The thing these pin is which way a pad is reached. A pad's engine address
// spells its rack component with the Drum Grid's own DeviceId, because that is
// what the ADSR macro links, the native parameter table and every saved link
// carry. Rack ids and device ids come out of counters that both start at 1, so
// that address cannot be resolved as a Rack step without sometimes landing on
// an unrelated rack. Every command reaches the pads through the device instead.

namespace {

void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    SelectionManager::getInstance().clearSelection();
}

DeviceInfo drumGridDevice() {
    DeviceInfo device;
    device.name = "Drum Grid";
    device.pluginId = "drumgrid";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

DeviceInfo padVoice(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "magdasampler";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

}  // namespace

TEST_CASE("A pad edit lands on the pad, not on a rack that shares its number",
          "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");

    // The collision the pad address invites: rack ids and device ids are handed
    // out by counters that both start at 1, so a Drum Grid and a rack on the
    // same track routinely share a number.
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());

    REQUIRE(rackId != INVALID_RACK_ID);
    REQUIRE(gridId != INVALID_DEVICE_ID);
    REQUIRE(rackChainId != INVALID_CHAIN_ID);

    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    // The rack numbered like the grid is the one that would be hit.
    const auto* rack = tm.getRack(trackId, gridId);
    INFO("rack " << gridId << (rack != nullptr ? " exists" : " does not exist"));

    constexpr int padIndex = 4;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    // On the pad.
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    REQUIRE(pad->elements.size() == 1);
    CHECK(getDevice(pad->elements[0]).id == voiceId);
    CHECK(getDevice(pad->elements[0]).name == "Kick");

    // And nowhere near the rack, whatever its number.
    if (rack != nullptr)
        for (const auto& chain : rack->chains)
            CHECK(chain.elements.empty());
}

TEST_CASE("A pad's fader and switches move the pad's own chain", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    REQUIRE(tm.setPadDevice(gridPath, padIndex, padVoice("Kick")) != INVALID_DEVICE_ID);

    tm.setPadVolume(gridPath, padIndex, -6.0f);
    tm.setPadPan(gridPath, padIndex, -0.5f);
    tm.setPadMuted(gridPath, padIndex, true);
    tm.setPadSolo(gridPath, padIndex, true);
    tm.setPadBypassed(gridPath, padIndex, true);

    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK(pad->volume == -6.0f);
    CHECK(pad->pan == -0.5f);
    CHECK(pad->muted);
    CHECK(pad->solo);
    CHECK(pad->bypassed);

    // The rack chain that shares the grid's number is untouched.
    const auto* rackChain = tm.getChain(trackId, rackId, rackChainId);
    REQUIRE(rackChain != nullptr);
    CHECK(rackChain->volume == 0.0f);
    CHECK(rackChain->pan == 0.0f);
    CHECK_FALSE(rackChain->muted);
    CHECK_FALSE(rackChain->solo);
}

TEST_CASE("A pad's chain takes devices, drops them and reorders them",
          "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 2;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    const auto padChainId = pad->id;

    DeviceInfo effect;
    effect.name = "Filter";
    effect.pluginId = "magda_filter";
    effect.format = PluginFormat::Internal;
    const auto effectId = tm.addDeviceToPad(gridPath, padChainId, effect);
    REQUIRE(effectId != INVALID_DEVICE_ID);
    CHECK(effectId != voiceId);

    {
        const auto* padNow = tm.getPad(gridPath, padIndex);
        REQUIRE(padNow->elements.size() == 2);
        CHECK(getDevice(padNow->elements[0]).id == voiceId);
        CHECK(getDevice(padNow->elements[1]).id == effectId);
    }

    tm.moveDeviceInPad(gridPath, padChainId, 1, 0);
    {
        const auto* padNow = tm.getPad(gridPath, padIndex);
        REQUIRE(padNow->elements.size() == 2);
        CHECK(getDevice(padNow->elements[0]).id == effectId);
        CHECK(getDevice(padNow->elements[1]).id == voiceId);
    }

    tm.removeDeviceFromPad(gridPath, padChainId, effectId);
    {
        const auto* padNow = tm.getPad(gridPath, padIndex);
        REQUIRE(padNow->elements.size() == 1);
        CHECK(getDevice(padNow->elements[0]).id == voiceId);
    }
}

TEST_CASE("Dropping an instrument on a pad replaces what it held", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 1;
    tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));

    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    DeviceInfo effect;
    effect.name = "Filter";
    effect.pluginId = "magda_filter";
    effect.format = PluginFormat::Internal;
    tm.addDeviceToPad(gridPath, pad->id, effect);
    REQUIRE(tm.getPad(gridPath, padIndex)->elements.size() == 2);

    // The pad's slot means the whole pad, effects and all.
    const auto snareId = tm.setPadDevice(gridPath, padIndex, padVoice("Snare"));
    const auto* padNow = tm.getPad(gridPath, padIndex);
    REQUIRE(padNow->elements.size() == 1);
    CHECK(getDevice(padNow->elements[0]).id == snareId);
    CHECK(getDevice(padNow->elements[0]).name == "Snare");
    CHECK(padNow->name == "Snare");
}

TEST_CASE("A pad is made on the note it answers to, and cleared off the grid",
          "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 7;
    REQUIRE(tm.ensurePad(gridPath, padIndex) != INVALID_CHAIN_ID);

    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK(pad->lowNote == padNoteFor(padIndex));
    CHECK(pad->highNote == padNoteFor(padIndex));
    CHECK(padParameterSlot(*pad) == padIndex);

    tm.clearPad(gridPath, padIndex);
    CHECK(tm.getPad(gridPath, padIndex) == nullptr);
}

TEST_CASE("Two pads trade what they answer to", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    const auto kickId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    const auto snareId = tm.setPadDevice(gridPath, 3, padVoice("Snare"));

    tm.swapPads(gridPath, 0, 3);

    const auto* atZero = tm.getPad(gridPath, 0);
    const auto* atThree = tm.getPad(gridPath, 3);
    REQUIRE(atZero != nullptr);
    REQUIRE(atThree != nullptr);
    CHECK(getDevice(atZero->elements[0]).id == snareId);
    CHECK(getDevice(atThree->elements[0]).id == kickId);

    // The chains stayed put and their ranges traded, so a chain id still names
    // the devices it always named.
    CHECK(atZero->lowNote == padNoteFor(0));
    CHECK(atThree->lowNote == padNoteFor(3));
}

TEST_CASE("A duplicated Drum Grid keeps nothing of the original's pad keys",
          "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    const auto copyTrackId = tm.duplicateTrack(trackId, true);
    REQUIRE(copyTrackId != INVALID_TRACK_ID);

    const auto* copyTrack = tm.getTrack(copyTrackId);
    REQUIRE(copyTrack != nullptr);
    REQUIRE_FALSE(copyTrack->chain.fxChainElements.empty());

    const auto& copiedGrid = getDevice(copyTrack->chain.fxChainElements[0]);
    CHECK(copiedGrid.id != gridId);
    REQUIRE(static_cast<bool>(copiedGrid.pads));

    // Both ids the copy carries are its own: the pad rack's, which is derived
    // from the device id, and each pad device's.
    CHECK(copiedGrid.pads->id == padRackIdFor(copiedGrid.id));

    const auto* copiedPad = findPadChain(*copiedGrid.pads.get(), padIndex);
    REQUIRE(copiedPad != nullptr);
    REQUIRE(copiedPad->elements.size() == 1);
    CHECK(getDevice(copiedPad->elements[0]).id != voiceId);
    CHECK(getDevice(copiedPad->elements[0]).id != INVALID_DEVICE_ID);
}

TEST_CASE("A duplicated grid's links follow its pads, and leave a like-numbered rack alone",
          "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");

    // A rack numbered like the grid, holding a device with a link pointing at
    // it. This is what a shared rack remap would corrupt.
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    DeviceInfo rackDevice;
    rackDevice.name = "Filter";
    rackDevice.pluginId = "magda_filter";
    rackDevice.format = PluginFormat::Internal;
    const auto rackDeviceId = tm.addDeviceToChain(trackId, rackId, rackChainId, rackDevice);
    REQUIRE(rackDeviceId != INVALID_DEVICE_ID);

    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);

    // The grid's own macro reaches into its pad, spelled the way a pad device
    // address is spelled: Rack(gridDeviceId) > Chain(pad) > Device(pad device).
    const auto padDevicePath = TrackManager::padChainPath(gridPath, pad->id).withDevice(voiceId);
    {
        auto* grid = tm.getDevice(trackId, gridId);
        REQUIRE(grid != nullptr);
        MacroLink link;
        link.target = ControlTarget::pluginParam(padDevicePath, 0);
        link.amount = 1.0f;
        grid->macros[0].links.push_back(link);
    }

    // A track macro reaching the rack device, through a rack whose number the
    // grid shares.
    const auto rackDevicePath =
        ChainNodePath::chainDevice(trackId, rackId, rackChainId, rackDeviceId);
    {
        auto* track = tm.getTrack(trackId);
        REQUIRE(track != nullptr);
        MacroLink link;
        link.target = ControlTarget::pluginParam(rackDevicePath, 0);
        link.amount = 1.0f;
        track->macros[0].links.push_back(link);
    }

    const auto copyTrackId = tm.duplicateTrack(trackId, true);
    REQUIRE(copyTrackId != INVALID_TRACK_ID);

    const auto* copyTrack = tm.getTrack(copyTrackId);
    REQUIRE(copyTrack != nullptr);

    const DeviceInfo* copiedGrid = nullptr;
    const RackInfo* copiedRack = nullptr;
    for (const auto& element : copyTrack->chain.fxChainElements) {
        if (isDevice(element) && getDevice(element).pads)
            copiedGrid = &getDevice(element);
        else if (isRack(element))
            copiedRack = &getRack(element);
    }
    REQUIRE(copiedGrid != nullptr);
    REQUIRE(copiedRack != nullptr);

    // The grid's link followed its pad onto the copy's own ids.
    const auto* copiedPad = findPadChain(*copiedGrid->pads.get(), padIndex);
    REQUIRE(copiedPad != nullptr);
    REQUIRE(copiedPad->elements.size() == 1);
    const auto copiedVoiceId = getDevice(copiedPad->elements[0]).id;

    REQUIRE_FALSE(copiedGrid->macros[0].links.empty());
    const auto& padLink = copiedGrid->macros[0].links[0].target.devicePath;
    REQUIRE(padLink.steps.size() == 3);
    CHECK(padLink.steps[0].id == copiedGrid->id);
    CHECK(padLink.steps[2].id == copiedVoiceId);
    CHECK(padLink.steps[2].id != voiceId);

    // And the rack's link followed the rack, not the grid: the two shared a
    // number, and a remap keyed on that number alone would have moved it.
    REQUIRE_FALSE(copyTrack->macros[0].links.empty());
    const auto& rackLink = copyTrack->macros[0].links[0].target.devicePath;
    REQUIRE(rackLink.steps.size() == 3);
    CHECK(rackLink.steps[0].id == copiedRack->id);

    // The chain the device was added to, which is not the rack's first: a rack
    // is made with one already.
    const ChainInfo* copiedRackChain = nullptr;
    for (const auto& chain : copiedRack->chains)
        if (!chain.elements.empty())
            copiedRackChain = &chain;
    REQUIRE(copiedRackChain != nullptr);

    CHECK(rackLink.steps[1].id == copiedRackChain->id);
    CHECK(rackLink.steps[2].id == getDevice(copiedRackChain->elements[0]).id);
    CHECK(rackLink.steps[2].id != rackDeviceId);
}

TEST_CASE("A pad device's power is model state", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK_FALSE(getDevice(pad->elements[0]).bypassed);

    // Powering a pad device off writes the model, which is what a save reads
    // and what the sync puts back onto the plugin. Toggling the plugin alone
    // used to be lost on reload.
    tm.setPadDeviceBypassed(gridPath, pad->id, voiceId, true);
    CHECK(getDevice(tm.getPad(gridPath, padIndex)->elements[0]).bypassed);

    tm.setPadDeviceBypassed(gridPath, pad->id, voiceId, false);
    CHECK_FALSE(getDevice(tm.getPad(gridPath, padIndex)->elements[0]).bypassed);
}
