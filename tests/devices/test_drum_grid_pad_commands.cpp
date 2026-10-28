// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/MacroInfo.hpp"
#include "magda/daw/core/PadCommands.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackCommands.hpp"
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

/// A pad's level in dB, which is its channel's fader (#3010).
float padLevelDb(const ChainNodePath& gridPath, int padIndex) {
    const auto* channel = TrackManager::getInstance().getPadChannel(gridPath, padIndex);
    REQUIRE(channel != nullptr);
    return juce::Decibels::gainToDecibels(channel->volume, -60.0f);
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

TEST_CASE("A pad's fader and switches land on its channel", "[drumgrid][pads][commands]") {
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

    // The mix is the pad's mixer channel's (#3010); bypass stays the pad's own.
    const auto* channel = tm.getPadChannel(gridPath, padIndex);
    REQUIRE(channel != nullptr);
    CHECK(padLevelDb(gridPath, padIndex) == Catch::Approx(-6.0f));
    CHECK(channel->pan == -0.5f);
    CHECK(channel->muted);
    CHECK(channel->soloed);
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
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

TEST_CASE("A pad's key range is model state", "[drumgrid][pads][commands]") {
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    constexpr int padIndex = 5;
    REQUIRE(tm.setPadDevice(gridPath, padIndex, padVoice("Clap")) != INVALID_DEVICE_ID);

    // A range arrives low-first whichever way round the sliders were dragged.
    // Widened over the two notes above it, the pad still answers to its own.
    const auto note = padNoteFor(padIndex);
    tm.setPadNoteRange(gridPath, padIndex, note + 2, note, note + 1);
    const auto* pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK(pad->lowNote == note);
    CHECK(pad->highNote == note + 2);
    CHECK(pad->rootNote == note + 1);

    // A range spanning everything MIDI can spell reaches outside the grid at
    // both ends, and is refused whole rather than clamped into it.
    CHECK_FALSE(tm.setPadNoteRange(gridPath, padIndex, -5, 300, 400));
    pad = tm.getPad(gridPath, padIndex);
    REQUIRE(pad != nullptr);
    CHECK(pad->lowNote == note);
    CHECK(pad->highNote == note + 2);

    // The root is clamped rather than refused: it is not an endpoint.
    CHECK(tm.setPadNoteRange(gridPath, padIndex, note, note + 2, 400));
    CHECK(tm.getPad(gridPath, padIndex)->rootNote == 127);
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
    CHECK(tm.getPadChannel(gridPath, padIndex)->muted);

    REQUIRE(undo.undo());
    CHECK_FALSE(tm.getPadChannel(gridPath, padIndex)->muted);

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
    CHECK(tm.getPadChannel(gridPath, padIndex)->muted);
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

    using Target = SetPadFaderCommand::Target;

    for (float db : {-1.0f, -2.0f, -3.0f, -4.0f})
        setPadFader(gridPath, padIndex, Target::Volume, db, /*gesture=*/1);

    CHECK(padLevelDb(gridPath, padIndex) == Catch::Approx(-4.0f));

    // The next drag is a new gesture, so it does not fold into the last one.
    // UndoManager merges adjacent commands with no timeout of its own, so
    // without the gesture one Undo would walk back both drags.
    setPadFader(gridPath, padIndex, Target::Volume, -8.0f, /*gesture=*/2);

    REQUIRE(undo.undo());
    CHECK(padLevelDb(gridPath, padIndex) == Catch::Approx(-4.0f));

    // One step back to where the first drag started, not four.
    REQUIRE(undo.undo());
    CHECK(padLevelDb(gridPath, padIndex) == Catch::Approx(0.0f).margin(1e-4));

    // And pan is its own step, not the level's.
    setPadFader(gridPath, padIndex, Target::Pan, -0.5f, /*gesture=*/3);
    setPadFader(gridPath, padIndex, Target::Volume, -2.0f, /*gesture=*/3);
    REQUIRE(undo.undo());
    CHECK(padLevelDb(gridPath, padIndex) == Catch::Approx(0.0f).margin(1e-4));
    CHECK(tm.getPadChannel(gridPath, padIndex)->pan == -0.5f);
}

TEST_CASE("A pad's range cannot reach a note another pad already answers to",
          "[drumgrid][pads][commands]") {
    // Every chain whose range covers an incoming note plays it, and every row,
    // fader and switch addresses a pad by finding the chain covering its note.
    // A second claimant would leave a row editing a chain other than the one it
    // shows, so the range is refused instead.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    REQUIRE(tm.setPadDevice(gridPath, 2, padVoice("Snare")) != INVALID_DEVICE_ID);

    const auto note = padNoteFor(0);

    // Up to, but not into, pad 2.
    CHECK(tm.setPadNoteRange(gridPath, 0, note, note + 1, note));
    CHECK(tm.getPad(gridPath, 0)->highNote == note + 1);

    // One note further would reach pad 2's, and is refused whole.
    CHECK_FALSE(tm.setPadNoteRange(gridPath, 0, note, note + 2, note));
    CHECK(tm.getPad(gridPath, 0)->highNote == note + 1);
}

TEST_CASE("A pad's range has to stay inside the notes the grid shows",
          "[drumgrid][pads][commands]") {
    // The grid shows kPadCount pads from kPadBaseNote and builds its rows from
    // those notes. A chain retuned clear of them keeps playing and disappears
    // from the UI, with no way left to edit or delete it.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto note = padNoteFor(0);

    CHECK_FALSE(tm.padNoteRangeIsFree(gridPath, 0, 0, kPadBaseNote - 1));
    CHECK_FALSE(tm.setPadNoteRange(gridPath, 0, 0, kPadBaseNote - 1, 0));
    CHECK(tm.getPad(gridPath, 0)->lowNote == note);

    const auto lastNote = padNoteFor(kPadCount - 1);
    CHECK_FALSE(tm.setPadNoteRange(gridPath, 0, lastNote + 1, 127, lastNote + 1));
    CHECK(tm.getPad(gridPath, 0)->lowNote == note);

    // Both ends, not merely an overlap. A low end below the grid makes
    // padParameterSlot() answer -1, and the plan stops binding the chain's
    // fader and pan to the grid's parameters.
    CHECK_FALSE(tm.setPadNoteRange(gridPath, 0, 0, note, 0));
    CHECK_FALSE(tm.setPadNoteRange(gridPath, 0, note, 127, note));
    CHECK(tm.getPad(gridPath, 0)->lowNote == note);
    CHECK(tm.getPad(gridPath, 0)->highNote == note);

    // The root is not an endpoint: it is the transposition target, and a sample
    // whose natural pitch sits outside the displayed pads is a valid mapping.
    CHECK(tm.setPadNoteRange(gridPath, 0, note, note, 127));
    CHECK(tm.getPad(gridPath, 0)->rootNote == 127);
}

TEST_CASE("A copied Drum Grid's pads get ids of their own", "[drumgrid][pads][commands]") {
    // The copy path allocates a new id for the outer grid. Without descending
    // into its pads the clone kept every pad DeviceId from the source, so the
    // plan would emit two devices onto one op and findDevicePath() would answer
    // whichever copy it met first.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto voiceId = tm.setPadDevice(gridPath, 0, padVoice("Kick"));
    REQUIRE(voiceId != INVALID_DEVICE_ID);
    const auto padChainId = tm.getPad(gridPath, 0)->id;

    // A macro on the grid pointing at its own pad device: what the copy has to
    // carry across to the copy's pad rather than leave on the original's.
    {
        auto* grid = tm.getDeviceInChainByPath(gridPath);
        REQUIRE(grid != nullptr);
        MacroLink link;
        link.target = ControlTarget::pluginParam(
            TrackManager::padChainPath(gridPath, padChainId).withDevice(voiceId), 0);
        link.amount = 1.0f;
        grid->macros[0].links.push_back(link);
    }

    std::vector<ChainElement> copied;
    copied.push_back(makeDeviceElement(*tm.getDeviceInChainByPath(gridPath)));
    REQUIRE(tm.insertChainElementsByPath(ChainNodePath::trackLevel(trackId), std::move(copied), 1,
                                         /*reassignIds=*/true));

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chain.fxChainElements.size() == 2);

    const auto& clone = getDevice(track->chain.fxChainElements[1]);
    CHECK(clone.id != gridId);
    REQUIRE(clone.pads);
    REQUIRE(clone.pads->chains.size() == 1);

    const auto& clonedVoice = getDevice(clone.pads->chains[0].elements[0]);
    CHECK(clonedVoice.id != voiceId);

    // The pad rack carries the clone's own id, and the copied macro names the
    // clone's pad rather than the original's.
    CHECK(clone.pads->id == padRackIdFor(clone.id));
    const auto clonePath = ChainNodePath::topLevelDevice(trackId, clone.id);
    CHECK(
        clone.macros[0].links[0].target.devicePath ==
        TrackManager::padChainPath(clonePath, clone.pads->chains[0].id).withDevice(clonedVoice.id));

    // And each id now resolves to its own copy.
    CHECK(tm.findDevicePath(voiceId) ==
          TrackManager::padChainPath(gridPath, padChainId).withDevice(voiceId));
    CHECK(tm.findDevicePath(clonedVoice.id).getDeviceId() == clonedVoice.id);
}

TEST_CASE("A copied link into a rack inside a pad follows the copy", "[drumgrid][pads][commands]") {
    // A pad's chain holds racks like any other, so a link can name something
    // deeper than the pad's own devices. Its prefix is still the pad's and has
    // to come from the pad remap; what follows moves with the ordinary maps.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);

    const auto padChainId = tm.getPad(gridPath, 0)->id;
    constexpr RackId kInnerRack = 77;
    constexpr ChainId kInnerChain = 3;
    constexpr DeviceId kNested = 4242;

    {
        RackInfo inner;
        inner.id = kInnerRack;
        ChainInfo innerChain;
        innerChain.id = kInnerChain;
        auto nested = padVoice("Nested");
        nested.id = kNested;
        innerChain.elements.push_back(makeDeviceElement(nested));
        inner.chains.push_back(std::move(innerChain));
        tm.getPadChain(gridPath, padChainId)->elements.push_back(makeRackElement(std::move(inner)));

        auto* grid = tm.getDeviceInChainByPath(gridPath);
        REQUIRE(grid != nullptr);
        MacroLink link;
        link.target = ControlTarget::pluginParam(TrackManager::padChainPath(gridPath, padChainId)
                                                     .withRack(kInnerRack)
                                                     .withChain(kInnerChain)
                                                     .withDevice(kNested),
                                                 0);
        link.amount = 1.0f;
        grid->macros[0].links.push_back(link);
    }

    std::vector<ChainElement> copied;
    copied.push_back(makeDeviceElement(*tm.getDeviceInChainByPath(gridPath)));
    REQUIRE(tm.insertChainElementsByPath(ChainNodePath::trackLevel(trackId), std::move(copied), 1,
                                         /*reassignIds=*/true));

    const auto& clone = getDevice(tm.getTrack(trackId)->chain.fxChainElements[1]);
    const auto& clonedRack = getRack(clone.pads->chains[0].elements[1]);
    const auto& clonedNested = getDevice(clonedRack.chains[0].elements[0]);

    // Every id moved, and the link names the copy's own.
    CHECK(clonedRack.id != kInnerRack);
    CHECK(clonedNested.id != kNested);
    CHECK(clone.macros[0].links[0].target.devicePath ==
          TrackManager::padChainPath(ChainNodePath::topLevelDevice(trackId, clone.id),
                                     clone.pads->chains[0].id)
              .withRack(clonedRack.id)
              .withChain(clonedRack.chains[0].id)
              .withDevice(clonedNested.id));

    // And both copies resolve to their own nested device.
    CHECK(tm.getDeviceInChainByPath(clone.macros[0].links[0].target.devicePath) != nullptr);
    CHECK(tm.findDevicePath(kNested).getDeviceId() == kNested);
}

TEST_CASE("A pad chain answering to more than one note can still be deleted",
          "[drumgrid][pads][commands]") {
    // clearPad() is the pad's own delete and leaves a chain shared with its
    // neighbours alone, which after a range edit left a widened chain with no
    // way off the grid at all.
    resetState();
    auto& tm = TrackManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);

    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto note = padNoteFor(0);
    REQUIRE(tm.setPadNoteRange(gridPath, 0, note, note + 2, note));

    const auto padChainId = tm.getPad(gridPath, 0)->id;

    // The pad's own clear refuses it: the chain is its neighbours' sound too.
    tm.clearPad(gridPath, 0);
    CHECK(tm.getPad(gridPath, 0) != nullptr);

    // The chain's delete takes it.
    tm.removePadChain(gridPath, padChainId);
    CHECK(tm.getPad(gridPath, 0) == nullptr);
    CHECK(tm.getPad(gridPath, 1) == nullptr);
    CHECK(tm.getPad(gridPath, 2) == nullptr);
}

TEST_CASE("A rack is made in a pad's chain and undoes in one step",
          "[drumgrid][pads][racks][3144]") {
    // Layering a pad is an ordinary rack in its chain, so creation has to take
    // a pad chain as the parent.
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& undo = UndoManager::getInstance();

    const auto trackId = tm.createTrack("Drums");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGridDevice());
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.setPadDevice(gridPath, 0, padVoice("Kick")) != INVALID_DEVICE_ID);
    const auto padPath = TrackManager::padChainPath(gridPath, tm.getPad(gridPath, 0)->id);

    auto command = std::make_unique<AddRackByPathCommand>(padPath, "Layers");
    auto* raw = command.get();
    undo.executeCommand(std::move(command));
    const auto rackId = raw->getCreatedRackId();
    REQUIRE(rackId != INVALID_RACK_ID);

    const auto rackPath = padPath.withRack(rackId);
    const auto* rack = tm.getRackByPath(rackPath);
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 1);
    CHECK(tm.getPad(gridPath, 0)->elements.size() == 2);

    // Its chain takes a device like any rack chain.
    const auto chainPath = rackPath.withChain(rack->chains.front().id);
    const auto snareId = tm.addDeviceToChainByPath(chainPath, padVoice("Snare"));
    REQUIRE(snareId != INVALID_DEVICE_ID);
    CHECK(tm.findDevicePath(snareId) == chainPath.withDevice(snareId));

    REQUIRE(undo.undo());
    CHECK(tm.getRackByPath(rackPath) == nullptr);
    CHECK(tm.getPad(gridPath, 0)->elements.size() == 1);

    REQUIRE(undo.redo());
    CHECK(tm.getRackByPath(rackPath) != nullptr);
}
