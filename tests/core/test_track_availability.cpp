// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/ChainNodePath.hpp"
#include "core/DrumGridPads.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackAvailability.hpp"
#include "core/TrackManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

using namespace magda;

namespace {

DeviceInfo makeDevice(DeviceId id, const juce::String& pluginId,
                      PluginFormat format = PluginFormat::Internal) {
    DeviceInfo device;
    device.id = id;
    device.name = "Device " + juce::String(id);
    device.pluginId = pluginId;
    device.format = format;
    return device;
}

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.audioOutputDevice = "master";
    return track;
}

/// A Drum Grid whose one pad holds @p onPad.
DeviceInfo makeDrumGrid(DeviceId id, DeviceInfo onPad) {
    auto grid = makeDevice(id, "drumgrid");
    auto rack = std::make_unique<RackInfo>();
    rack->id = padRackIdFor(id);
    ChainInfo pad;
    pad.id = 0;
    pad.elements.push_back(makeDeviceElement(std::move(onPad)));
    rack->chains.push_back(std::move(pad));
    grid.pads.reset(std::move(rack));
    return grid;
}

/// A rack whose one chain holds @p inside.
RackInfo makeRack(RackId id, DeviceInfo inside) {
    RackInfo rack;
    rack.id = id;
    ChainInfo chain;
    chain.id = 1;
    chain.elements.push_back(makeDeviceElement(std::move(inside)));
    rack.chains.push_back(std::move(chain));
    return rack;
}

struct TrackManagerScope {
    TrackManagerScope() {
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
    }
    ~TrackManagerScope() {
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
    }
};

}  // namespace

TEST_CASE("Only a MAGDA device no catalog knows is unregistered", "[core][tracks][3101]") {
    CHECK(isUnregisteredDevice(makeDevice(1, "from_a_newer_build")));
    CHECK_FALSE(isUnregisteredDevice(makeDevice(1, "drumgrid")));

    // An unset slot, and an external plugin, which is a file rather than a catalog entry.
    CHECK_FALSE(isUnregisteredDevice(makeDevice(1, "")));
    CHECK_FALSE(isUnregisteredDevice(makeDevice(1, "VST3-1234", PluginFormat::VST3)));
}

TEST_CASE("A missing device is found wherever the chain holds it", "[core][tracks][3101]") {
    const auto missing = makeDevice(9, "from_a_newer_build");
    auto track = makeTrack(4);

    SECTION("at the top") {
        track.chain.fxChainElements.push_back(makeDeviceElement(missing));
    }
    SECTION("in a rack") {
        track.chain.fxChainElements.push_back(makeRackElement(makeRack(2, missing)));
    }
    SECTION("on a drum pad") {
        track.chain.fxChainElements.push_back(makeDeviceElement(makeDrumGrid(3, missing)));
    }
    SECTION("post-FX") {
        track.chain.postFxChainElements.push_back({.device = missing});
    }

    const auto found = unregisteredDevicesOn(track);
    REQUIRE(found.size() == 1);
    CHECK(found.front()->name == missing.name);

    auto master = makeTrack(MASTER_TRACK_ID);
    CHECK(tracksHoldingUnregisteredDevices({track, makeTrack(5)}, master) == std::set<TrackId>{4});
}

TEST_CASE("A track holding a missing device stays off until the device goes",
          "[core][tracks][3101]") {
    TrackManagerScope scope;
    auto& tracks = TrackManager::getInstance();

    const auto trackId = tracks.createTrack("Held", TrackType::Media);
    const auto deviceId = tracks.addDeviceToTrack(trackId, makeDevice(0, "from_a_newer_build"));
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    CHECK_FALSE(tracks.isTrackEnabled(trackId));
    CHECK(tracks.getUnavailableDeviceNames(trackId).size() == 1);

    // Refused, and the track's own switch is left as the project saved it.
    CHECK_FALSE(tracks.setTrackEnabled(trackId, true));
    CHECK(switchTrackEnabled(trackId, true).isNotEmpty());
    CHECK(tracks.getTrack(trackId)->enabled);

    tracks.removeDeviceFromChainByPath(ChainNodePath::topLevelDevice(trackId, deviceId));
    CHECK(tracks.isTrackEnabled(trackId));
    CHECK(tracks.getUnavailableDeviceNames(trackId).isEmpty());
}

TEST_CASE("Switching a track off is undoable", "[core][tracks][3101]") {
    TrackManagerScope scope;
    auto& tracks = TrackManager::getInstance();
    const auto trackId = tracks.createTrack("Plain", TrackType::Media);

    CHECK(switchTrackEnabled(trackId, false).isEmpty());
    CHECK_FALSE(tracks.isTrackEnabled(trackId));

    UndoManager::getInstance().undo();
    CHECK(tracks.isTrackEnabled(trackId));
}
