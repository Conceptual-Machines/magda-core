// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "StructuralRoundTrip.hpp"
#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/PresetManager.hpp"
#include "magda/daw/core/RackDevices.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

// The device browser lists racks as devices (#3417): a rack device handed to any device add
// lands as the rack it stands for.

namespace {

using namespace magda;
using magda::structural_test::resetState;

/** @brief Points the presets folder at a fresh temporary one for the test's duration. */
struct PresetDirectoryScope {
    PresetDirectoryScope()
        : previous(Config::getInstance().getPresetsDir()),
          directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("magda-rack-devices", {})) {
        REQUIRE(directory.createDirectory());
        Config::getInstance().setPresetsDir(directory.getFullPathName().toStdString());
        paths::resolve();
    }

    ~PresetDirectoryScope() {
        Config::getInstance().setPresetsDir(previous);
        paths::resolve();
        directory.deleteRecursively();
    }

    std::string previous;
    juce::File directory;
};

DeviceInfo rackDevice(const juce::String& pluginId) {
    DeviceInfo device;
    device.name = "Rack";
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;
    return device;
}

DeviceInfo effect(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "delay";
    device.format = PluginFormat::Internal;
    return device;
}

DeviceInfo drumGrid() {
    DeviceInfo device;
    device.name = "Drum Grid";
    device.pluginId = "drumgrid";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

const RackInfo* rackIn(const std::vector<ChainElement>& elements, size_t index) {
    if (index >= elements.size() || !isRack(elements[index]))
        return nullptr;
    return &getRack(elements[index]);
}

}  // namespace

TEST_CASE("A rack device added to a track becomes a rack", "[racks][browser]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Resident"));

    CHECK(tm.addDeviceToTrack(trackId, rackDevice(kRackDeviceId), 0) == INVALID_DEVICE_ID);
    tm.addDeviceToTrack(trackId, rackDevice(kMultibandRackDeviceId));

    const auto& elements = tm.getChainElements(trackId);
    REQUIRE(elements.size() == 3);
    const auto* rack = rackIn(elements, 0);
    REQUIRE(rack != nullptr);
    CHECK(rack->name == "Rack");
    CHECK_FALSE(rack->multiband);
    CHECK(rack->chains.size() == 1);

    const auto* multiband = rackIn(elements, 2);
    REQUIRE(multiband != nullptr);
    CHECK(multiband->multiband);
    CHECK(multiband->chains.size() == multiband->crossovers.size() + 1);
    CHECK(multiband->id != rack->id);
}

TEST_CASE("A rack preset device inserts the preset's rack, fresh each time", "[racks][browser]") {
    PresetDirectoryScope scope;
    resetState();
    auto& tm = TrackManager::getInstance();
    auto& presets = PresetManager::getInstance();
    const auto trackId = tm.createTrack("Track");

    RackInfo saved;
    saved.name = "Stack";
    ChainInfo chain;
    chain.elements.push_back(makeDeviceElement(effect("Inside")));
    saved.chains.push_back(std::move(chain));
    REQUIRE(presets.saveRackPreset(saved, "Group/Stack"));
    REQUIRE(presets.getRackPresets().contains("Group/Stack"));
    CHECK_FALSE(presets.isMultibandRackPreset("Group/Stack"));

    const auto device = rackDevice(rackPresetDeviceId("Group/Stack"));
    REQUIRE(isRackDevice(device));
    tm.addDeviceToTrack(trackId, device);
    tm.addDeviceToTrack(trackId, device);

    const auto& elements = tm.getChainElements(trackId);
    REQUIRE(elements.size() == 2);
    const auto* first = rackIn(elements, 0);
    const auto* second = rackIn(elements, 1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->name == "Stack");
    CHECK(first->id != second->id);
    REQUIRE(first->chains.size() == 1);
    CHECK(first->chains.front().elements.size() == 1);

    // A preset that no longer loads adds nothing.
    tm.addDeviceToTrack(trackId, rackDevice(rackPresetDeviceId("Missing")));
    CHECK(tm.getChainElements(trackId).size() == 2);
}

TEST_CASE("A multiband rack preset says so without being loaded", "[racks][browser]") {
    PresetDirectoryScope scope;
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");
    const auto rackId = tm.addMultibandRackToTrack(trackId);
    const auto* rack = tm.getRack(trackId, rackId);
    REQUIRE(rack != nullptr);
    REQUIRE(PresetManager::getInstance().saveRackPreset(*rack, "Bands"));
    CHECK(PresetManager::getInstance().isMultibandRackPreset("Bands"));
}

TEST_CASE("A rack device lands in a rack's chain and on a Drum Grid pad", "[racks][browser]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");

    SECTION("a rack's chain") {
        const auto outerId = tm.addRackToTrack(trackId);
        const auto* outer = tm.getRack(trackId, outerId);
        REQUIRE(outer != nullptr);
        const auto chainPath = ChainNodePath::chain(trackId, outerId, outer->chains.front().id);
        tm.addDeviceToChainByPath(chainPath, effect("Inside"), 0);
        tm.addDeviceToChainByPath(chainPath, rackDevice(kRackDeviceId), 0);

        const auto* chain = tm.getChainByPath(chainPath);
        REQUIRE(chain != nullptr);
        REQUIRE(chain->elements.size() == 2);
        CHECK(rackIn(chain->elements, 0) != nullptr);
    }

    SECTION("a pad, replacing what it held") {
        const auto gridId = tm.addDeviceToTrack(trackId, drumGrid());
        const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
        tm.setPadDevice(gridPath, 0, effect("Old"));
        tm.setPadDevice(gridPath, 0, rackDevice(kRackDeviceId));

        const auto* pad = tm.getPad(gridPath, 0);
        REQUIRE(pad != nullptr);
        REQUIRE(pad->elements.size() == 1);
        CHECK(rackIn(pad->elements, 0) != nullptr);
    }
}

TEST_CASE("Post-fader FX takes no rack device", "[racks][browser]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");
    CHECK(tm.addDeviceToPostFx(trackId, rackDevice(kRackDeviceId)) == INVALID_DEVICE_ID);
    CHECK(tm.getPostFxChainElements(trackId).empty());
}

TEST_CASE("Adding a rack device to a track undoes as one step", "[racks][browser]") {
    resetState();
    auto& tm = TrackManager::getInstance();
    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("Resident"));

    UndoManager::getInstance().executeCommand(
        std::make_unique<AddDeviceToTrackCommand>(trackId, rackDevice(kRackDeviceId)));
    REQUIRE(tm.getChainElements(trackId).size() == 2);
    CHECK(rackIn(tm.getChainElements(trackId), 1) != nullptr);

    REQUIRE(UndoManager::getInstance().undo());
    CHECK(tm.getChainElements(trackId).size() == 1);
}
