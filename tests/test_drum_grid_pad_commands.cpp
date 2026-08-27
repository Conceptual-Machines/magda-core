#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/MacroInfo.hpp"
#include "magda/daw/core/PadCommands.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

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
    UndoManager::getInstance().clearHistory();
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

namespace {

/// A macro link on @p macros pointing at @p path.
void linkTo(MacroArray& macros, const ChainNodePath& path) {
    MacroLink link;
    link.target = ControlTarget::pluginParam(path, 0);
    link.amount = 1.0f;
    macros[0].links.push_back(link);
}

const ChainNodePath& firstLinkPath(const MacroArray& macros) {
    return macros[0].links[0].target.devicePath;
}

}  // namespace

TEST_CASE("A duplicated grid's pad links follow the pads, whoever owns the link",
          "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");

    // Two racks, so the grid's NEW device id will collide with an OLD rack id.
    // That is what turns a second pass over an already-rewritten pad path into
    // corruption, and what a shared rack remap would hit on the first pass.
    // Enough racks that a rack id reaches the number the COPY's grid device
    // will be given. That is what makes the double-remap reachable: a pad path
    // rewritten once, then run through the ordinary rack remap again, would be
    // sent to the rack sharing the copy's grid id.
    std::vector<RackId> rackIds;
    for (int i = 0; i < 6; ++i)
        rackIds.push_back(tm.addRackToTrack(trackId, "Rack " + juce::String(i)));
    for (auto id : rackIds)
        REQUIRE(id != INVALID_RACK_ID);

    const auto rackA = rackIds.front();
    const auto rackB = rackIds.back();

    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackB));
    DeviceInfo rackDevice;
    rackDevice.name = "Filter";
    rackDevice.pluginId = "magda_filter";
    rackDevice.format = PluginFormat::Internal;
    const auto rackDeviceId = tm.addDeviceToChain(trackId, rackB, rackChainId, rackDevice);
    REQUIRE(rackDeviceId != INVALID_DEVICE_ID);

    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);

    const auto padDevicePath = TrackManager::padChainPath(gridPath, pad->id).withDevice(voiceId);
    const auto rackDevicePath =
        ChainNodePath::chainDevice(trackId, rackB, rackChainId, rackDeviceId);

    // The same pad, linked from all three scopes a link can live in.
    {
        auto* grid = tm.getDevice(trackId, gridId);
        auto* rack = tm.getRack(trackId, rackA);
        auto* track = tm.getTrack(trackId);
        REQUIRE(grid != nullptr);
        REQUIRE(rack != nullptr);
        REQUIRE(track != nullptr);

        linkTo(grid->macros, padDevicePath);
        linkTo(rack->macros, padDevicePath);
        linkTo(track->macros, padDevicePath);

        // And a track link at the rack device, to prove a path that merely
        // passes through a rack numbered like a grid is left alone.
        MacroLink rackLink;
        rackLink.target = ControlTarget::pluginParam(rackDevicePath, 0);
        track->macros[1].links.push_back(rackLink);
    }

    const auto copyTrackId = tm.duplicateTrack(trackId, true);
    REQUIRE(copyTrackId != INVALID_TRACK_ID);

    const auto* copyTrack = tm.getTrack(copyTrackId);
    REQUIRE(copyTrack != nullptr);

    const DeviceInfo* copiedGrid = nullptr;
    std::vector<const RackInfo*> copiedRacks;
    for (const auto& element : copyTrack->chain.fxChainElements) {
        if (isDevice(element) && getDevice(element).pads)
            copiedGrid = &getDevice(element);
        else if (isRack(element))
            copiedRacks.push_back(&getRack(element));
    }
    REQUIRE(copiedGrid != nullptr);
    REQUIRE(copiedRacks.size() == rackIds.size());

    // The precondition this case exists for: the copy's grid id is a number an
    // ORIGINAL rack had, so a pad path rewritten once and then run through the
    // ordinary rack remap again would be sent to that rack. If the allocators
    // ever stop colliding, this says so rather than passing on nothing.
    INFO("copied grid id " << copiedGrid->id);
    CHECK(std::find(rackIds.begin(), rackIds.end(), copiedGrid->id) != rackIds.end());

    const auto* copiedPad = findPadChain(*copiedGrid->pads.get(), padIndex);
    REQUIRE(copiedPad != nullptr);
    REQUIRE(copiedPad->elements.size() == 1);
    const auto copiedVoiceId = getDevice(copiedPad->elements[0]).id;
    CHECK(copiedVoiceId != voiceId);

    // Every link into the pad, from every scope, lands on the copy's pad.
    const auto expectPadLink = [&](const MacroArray& macros, const char* owner) {
        INFO("link owner: " << owner);
        REQUIRE_FALSE(macros[0].links.empty());
        const auto& path = firstLinkPath(macros);
        REQUIRE(path.steps.size() == 3);
        CHECK(path.trackId == copyTrackId);
        CHECK(path.steps[0].id == copiedGrid->id);
        CHECK(path.steps[1].id == copiedPad->id);
        CHECK(path.steps[2].id == copiedVoiceId);
    };

    expectPadLink(copiedGrid->macros, "the grid");
    expectPadLink(copiedRacks[0]->macros, "a rack");
    expectPadLink(copyTrack->macros, "the track");

    // And the rack link followed the rack, not the grid.
    const RackInfo* copiedRackB = nullptr;
    for (const auto* rack : copiedRacks)
        for (const auto& chain : rack->chains)
            if (!chain.elements.empty())
                copiedRackB = rack;
    REQUIRE(copiedRackB != nullptr);

    REQUIRE_FALSE(copyTrack->macros[1].links.empty());
    const auto& rackLinkPath = copyTrack->macros[1].links[0].target.devicePath;
    REQUIRE(rackLinkPath.steps.size() == 3);
    CHECK(rackLinkPath.steps[0].id == copiedRackB->id);
    CHECK(rackLinkPath.steps[2].id != rackDeviceId);
}

TEST_CASE("A pad device's gain is model state", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Kick"));
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);

    tm.setPadDeviceGainDb(gridPath, pad->id, voiceId, -6.0f);

    const auto& device = getDevice(tm.getPad(gridPath, padIndex)->elements[0]);
    CHECK(device.gainDb == -6.0f);

    // Both, because the audio path reads the linear one and the slider the dB.
    CHECK(device.gainValue > 0.50f);
    CHECK(device.gainValue < 0.51f);
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

// ============================================================================
// #2211 -- what the pad flip left unfinished
// ============================================================================

TEST_CASE("A pad device is found by id like any other device", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 3;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Snare"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);

    // The address a stored link carries: the grid's own DeviceId in the rack
    // step, then the pad chain, then the device.
    CHECK(tm.findDevicePath(voiceId) ==
          TrackManager::padChainPath(gridPath, pad->id).withDevice(voiceId));

    // The devices it could already find still resolve the same way.
    CHECK(tm.findDevicePath(gridId) == gridPath);

    const auto rackDeviceId = tm.addDeviceToChainByPath(
        ChainNodePath::chain(trackId, rackId, rackChainId), padVoice("Rack Voice"));
    REQUIRE(rackDeviceId != INVALID_DEVICE_ID);
    CHECK(tm.findDevicePath(rackDeviceId) ==
          ChainNodePath::chainDevice(trackId, rackId, rackChainId, rackDeviceId));
}

TEST_CASE("A pad's output bus and key range are model state", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 5;
    REQUIRE(tm.setPadDevice(gridPath, padIndex, padVoice("Clap")) != INVALID_DEVICE_ID);

    tm.setPadOutput(gridPath, padIndex, 2);
    CHECK(tm.getPad(gridPath, padIndex)->outputIndex == 2);

    // A range arrives low-first whichever way round the sliders were dragged.
    // Widened over the two notes above it, the pad still answers to its own.
    const auto note = padNoteFor(padIndex);
    tm.setPadNoteRange(gridPath, padIndex, note + 2, note, note + 1);
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK(pad->lowNote == note);
    CHECK(pad->highNote == note + 2);
    CHECK(pad->rootNote == note + 1);

    // Clamped to what MIDI can spell.
    tm.setPadNoteRange(gridPath, padIndex, -5, 300, 400);
    pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK(pad->lowNote == 0);
    CHECK(pad->highNote == 127);
    CHECK(pad->rootNote == 127);
}

TEST_CASE("A pad moved off its own note stops being that pad", "[drumgrid][pads][commands]") {
    // The pad is the note, which is what `swapPads` trades. Retuning a chain
    // clear of the note it was on hands its sound to whichever pad the new
    // range covers, and leaves the old one empty.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 5;
    constexpr int movedTo = 9;
    REQUIRE(tm.setPadDevice(gridPath, padIndex, padVoice("Clap")) != INVALID_DEVICE_ID);

    const auto note = padNoteFor(movedTo);
    tm.setPadNoteRange(gridPath, padIndex, note, note, note);

    CHECK(tm.getPad(gridPath, padIndex) == nullptr);
    REQUIRE(tm.getPad(gridPath, movedTo) != nullptr);
    CHECK(tm.getPad(gridPath, movedTo)->elements.size() == 1);
}

TEST_CASE("A pad edit is one undo step", "[drumgrid][pads][commands][undo]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& undo = UndoManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    editPads(gridPath, "Set Pad Instrument", [gridPath]() {
        TrackManager::getInstance().setPadDevice(gridPath, padIndex, padVoice("Kick"));
    });
    REQUIRE(tm.getPad(gridPath, padIndex) != nullptr);
    REQUIRE(tm.getPad(gridPath, padIndex)->elements.size() == 1);
    const auto voiceId = getDevice(tm.getPad(gridPath, padIndex)->elements[0]).id;

    editPads(gridPath, "Mute Pad",
             [gridPath]() { TrackManager::getInstance().setPadMuted(gridPath, padIndex, true); });
    CHECK(tm.getPad(gridPath, padIndex)->muted);

    REQUIRE(undo.undo());
    CHECK_FALSE(tm.getPad(gridPath, padIndex)->muted);

    // The pad itself goes back, devices and all: the snapshot is the whole rack.
    REQUIRE(undo.undo());
    CHECK(tm.getPad(gridPath, padIndex) == nullptr);

    // Redo restores the snapshot rather than replaying the edit. Replaying an
    // add allocates a fresh DeviceId, and the mute behind it in the redo chain
    // still names the one the first run handed out.
    REQUIRE(undo.redo());
    REQUIRE(tm.getPad(gridPath, padIndex) != nullptr);
    REQUIRE(tm.getPad(gridPath, padIndex)->elements.size() == 1);
    CHECK(getDevice(tm.getPad(gridPath, padIndex)->elements[0]).id == voiceId);

    REQUIRE(undo.redo());
    CHECK(tm.getPad(gridPath, padIndex)->muted);
    CHECK(getDevice(tm.getPad(gridPath, padIndex)->elements[0]).id == voiceId);
}

TEST_CASE("A pad device resolves through the generic path lookup", "[drumgrid][pads][commands]") {
    // findDevicePath() answering with a pad address is only worth anything if
    // the address can then be dereferenced: alias generation, automation
    // targets and link repair all go id -> path -> DeviceInfo.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto rackId = tm.addRackToTrack(trackId, "FX Rack");
    const auto rackChainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 2;
    const auto voiceId = tm.setPadDevice(gridPath, padIndex, padVoice("Hat"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);

    const auto padDevicePath = tm.findDevicePath(voiceId);
    REQUIRE(padDevicePath.isValid());

    const auto* resolved = tm.getDeviceInChainByPath(padDevicePath);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->id == voiceId);
    CHECK(resolved->name == "Hat");

    // The rack sharing the grid's number still resolves the ordinary way; a pad
    // path is only tried once that has failed.
    const auto rackDeviceId = tm.addDeviceToChainByPath(
        ChainNodePath::chain(trackId, rackId, rackChainId), padVoice("Rack Voice"));
    const auto* rackResolved = tm.getDeviceInChainByPath(
        ChainNodePath::chainDevice(trackId, rackId, rackChainId, rackDeviceId));
    REQUIRE(rackResolved != nullptr);
    CHECK(rackResolved->id == rackDeviceId);

    // A path naming a device that is in neither place still answers nothing.
    CHECK(tm.getDeviceInChainByPath(ChainNodePath::chainDevice(trackId, gridId, 0, 9999)) ==
          nullptr);
}

TEST_CASE("A device inside a rack inside a pad resolves too", "[drumgrid][pads][commands]") {
    // A pad's chain holds elements like any other chain, racks included, and
    // every walk that reaches a pad recurses through them. The synthetic
    // prefix is the pad's; the rest of the address is an ordinary route.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 1;
    REQUIRE(tm.setPadDevice(gridPath, padIndex, padVoice("Tom")) != INVALID_DEVICE_ID);

    auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    const auto padChainId = pad->id;

    // Built on the model directly: no pad command makes one today, and the
    // point is that the address resolves wherever the model can express it.
    constexpr DeviceId kNested = 4242;
    RackInfo inner;
    inner.id = 77;
    inner.name = "Pad Rack";
    ChainInfo innerChain;
    innerChain.id = 3;
    auto nested = padVoice("Nested");
    nested.id = kNested;
    innerChain.elements.push_back(makeDeviceElement(nested));
    inner.chains.push_back(std::move(innerChain));
    tm.getPadChain(gridPath, padChainId)->elements.push_back(makeRackElement(std::move(inner)));

    const auto path = tm.findDevicePath(kNested);
    REQUIRE(path.isValid());
    CHECK(path == TrackManager::padChainPath(gridPath, padChainId)
                      .withRack(77)
                      .withChain(3)
                      .withDevice(kNested));

    const auto* resolved = tm.getDeviceInChainByPath(path);
    REQUIRE(resolved != nullptr);
    CHECK(resolved->id == kNested);
    CHECK(resolved->name == "Nested");
}

TEST_CASE("A dragged pad fader is one undo step, not one per move",
          "[drumgrid][pads][commands][undo]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& undo = UndoManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 0;
    editPads(gridPath, "Set Pad Instrument", [gridPath]() {
        TrackManager::getInstance().setPadDevice(gridPath, padIndex, padVoice("Kick"));
    });

    for (float db : {-1.0f, -2.0f, -3.0f, -4.0f})
        editPads(
            gridPath, "Set Pad Level",
            [gridPath, db]() { TrackManager::getInstance().setPadVolume(gridPath, padIndex, db); },
            "padLevel:0");

    CHECK(tm.getPad(gridPath, padIndex)->volume == -4.0f);

    // One step back to where the drag started, not four.
    REQUIRE(undo.undo());
    CHECK(tm.getPad(gridPath, padIndex)->volume == 0.0f);

    // A different pad's fader is its own step.
    CHECK(tm.getPad(gridPath, padIndex) != nullptr);
}
