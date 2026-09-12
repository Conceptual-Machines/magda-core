#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <variant>
#include <vector>

#include "core/AddressedParameters.hpp"
#include "core/ChainWalk.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"

/**
 * @file test_addressed_parameters.cpp
 * @brief What counts as addressing a parameter, and what does not (#2630).
 */

using namespace magda;

namespace {

DeviceInfo makeDevice(DeviceId id, int numParameters = 4) {
    DeviceInfo device;
    device.id = id;
    device.name = "Device " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.format = PluginFormat::VST3;
    device.macros = createDefaultMacros(2);
    device.mods = createDefaultMods(0);

    for (int index = 0; index < numParameters; ++index)
        device.parameters.emplace_back(index, "P" + juce::String(index), "", 0.0f, 1.0f, 0.0f);

    return device;
}

TrackInfo makeTrack(TrackId id = 1) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.macros = createDefaultMacros(2);
    track.mods = createDefaultMods(1);
    return track;
}

/// A track holding one device at track level, and that device's address.
struct OneDevice {
    TrackInfo track;
    ChainNodePath path;
};

OneDevice oneDevice(DeviceInfo device) {
    auto track = makeTrack();
    const auto path = chain_walk::deviceIn(ChainNodePath::trackLevel(track.id), device.id);
    track.chain.fxChainElements.push_back(makeDeviceElement(std::move(device)));
    return {std::move(track), path};
}

AddressedParameters addressedIn(const std::vector<TrackInfo>& tracks,
                                std::span<const AutomationLaneInfo> lanes = {},
                                std::span<const ControlTarget> bound = {}) {
    AddressingSources sources;
    sources.tracks = tracks;
    sources.lanes = lanes;
    sources.bound = bound;
    return AddressedParameters::from(sources);
}

AutomationLaneInfo laneOver(const ControlTarget& target,
                            AutomationAuthorityState state = AutomationAuthorityState::Reading) {
    AutomationLaneInfo lane;
    lane.id = 1;
    lane.target = target;
    lane.authorityState = state;
    return lane;
}

}  // namespace

TEST_CASE("A device nothing touches addresses nothing", "[core][parameters][addressed]") {
    const auto project = oneDevice(makeDevice(7));
    const auto addressed = addressedIn({project.track});

    CHECK(addressed.numDevices() == 0);
    CHECK(addressed.forDevice(project.path).empty());
    CHECK_FALSE(addressed.addresses(project.path, 0));
}

TEST_CASE("An automation lane addresses its parameter", "[core][parameters][addressed]") {
    const auto project = oneDevice(makeDevice(7));

    // A lane the model is not playing still holds the value the user drew
    // against, so its slot is mirrored like any other.
    const auto state =
        GENERATE(AutomationAuthorityState::Reading, AutomationAuthorityState::Disabled,
                 AutomationAuthorityState::Writing);

    const std::vector<AutomationLaneInfo> lanes{
        laneOver(ControlTarget::pluginParam(project.path, 2), state)};

    const auto addressed = addressedIn({project.track}, lanes);

    CHECK(addressed.addresses(project.path, 2));
    CHECK_FALSE(addressed.addresses(project.path, 1));
}

TEST_CASE("A macro or modifier link addresses what it names", "[core][parameters][addressed]") {
    auto project = oneDevice(makeDevice(7));

    SECTION("at track scope") {
        project.track.macros[0].links.push_back(
            {ControlTarget::pluginParam(project.path, 1), 1.0f});
    }
    SECTION("from a track modifier") {
        project.track.mods[0].addLink(ControlTarget::pluginParam(project.path, 1), 1.0f);
    }
    SECTION("from the device's own macro") {
        auto& device = std::get<DeviceInfo>(project.track.chain.fxChainElements[0]);
        device.macros[0].links.push_back({ControlTarget::pluginParam(project.path, 1), 1.0f});
    }

    const auto addressed = addressedIn({project.track});

    CHECK(addressed.addresses(project.path, 1));
    CHECK(addressed.forDevice(project.path).size() == 1);
}

TEST_CASE("A rack macro reaches a device in its chain", "[core][parameters][addressed]") {
    RackInfo rack;
    rack.id = 3;
    rack.macros = createDefaultMacros(2);
    rack.mods = createDefaultMods(0);

    ChainInfo chain;
    chain.id = 1;
    chain.elements.push_back(makeDeviceElement(makeDevice(7)));
    rack.chains.push_back(std::move(chain));

    auto track = makeTrack();
    const auto devicePath =
        ChainNodePath::trackLevel(track.id).withRack(rack.id).withChain(1).withDevice(7);
    rack.macros[0].links.push_back({ControlTarget::pluginParam(devicePath, 3), 1.0f});
    track.chain.fxChainElements.push_back(makeRackElement(std::move(rack)));

    const auto addressed = addressedIn({track});

    CHECK(addressed.addresses(devicePath, 3));
}

TEST_CASE("A controller binding addresses its parameter", "[core][parameters][addressed]") {
    const auto project = oneDevice(makeDevice(7));
    const std::vector<ControlTarget> bound{ControlTarget::pluginParam(project.path, 0)};

    const auto addressed = addressedIn({project.track}, {}, bound);

    CHECK(addressed.addresses(project.path, 0));
}

TEST_CASE("A place in the device UI addresses a parameter", "[core][parameters][addressed]") {
    auto device = makeDevice(7);
    device.visibleParameters = {3, 1};
    device.miniMixerParameters = {0};
    device.aiSoundDesignerParameters = {2};

    const auto project = oneDevice(std::move(device));
    const auto addressed = addressedIn({project.track});

    const auto slots = addressed.forDevice(project.path);
    REQUIRE(slots.size() == 4);
    CHECK(std::vector<int>(slots.begin(), slots.end()) == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("A UI list holds array positions, not slots", "[core][parameters][addressed]") {
    // A hosted plugin's own parameters start at slot 2: the wrapper pair holds
    // 0 and 1 and lives in its own array (ExternalPluginState.hpp). The config
    // dialog writes positions in DeviceInfo::parameters, so position 0 is the
    // plugin's first automatable parameter and the slot it addresses is 2.
    auto device = makeDevice(7, 0);
    for (int slot = 2; slot < 6; ++slot)
        device.parameters.emplace_back(slot, "P" + juce::String(slot), "", 0.0f, 1.0f, 0.0f);

    device.visibleParameters = {0, 3};

    // Past the end of the array: a stale config, which addresses nothing rather
    // than a slot that is not there.
    device.miniMixerParameters = {9};

    const auto project = oneDevice(std::move(device));
    const auto addressed = addressedIn({project.track});

    const auto slots = addressed.forDevice(project.path);
    REQUIRE(slots.size() == 2);
    CHECK(std::vector<int>(slots.begin(), slots.end()) == std::vector<int>{2, 5});
}

TEST_CASE("An empty visible list addresses nothing", "[core][parameters][addressed]") {
    // The UI reads "show what the device has" off the instance (#2634). Taking
    // it as addressing every slot would mirror the whole array again for any
    // plugin the user has not configured, which is every plugin by default.
    const auto project = oneDevice(makeDevice(7));
    const auto addressed = addressedIn({project.track});

    CHECK(addressed.forDevice(project.path).empty());
}

TEST_CASE("A slot addressed twice is listed once", "[core][parameters][addressed]") {
    auto project = oneDevice(makeDevice(7));
    project.track.macros[0].links.push_back({ControlTarget::pluginParam(project.path, 2), 1.0f});
    project.track.macros[1].links.push_back({ControlTarget::pluginParam(project.path, 2), 0.5f});

    const std::vector<AutomationLaneInfo> lanes{
        laneOver(ControlTarget::pluginParam(project.path, 2))};

    const auto addressed = addressedIn({project.track}, lanes);

    const auto slots = addressed.forDevice(project.path);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0] == 2);
}

TEST_CASE("Addressing something other than a plugin parameter addresses no slot",
          "[core][parameters][addressed]") {
    auto project = oneDevice(makeDevice(7));
    project.track.macros[0].links.push_back({ControlTarget::deviceMacro(project.path, 0), 1.0f});

    const std::vector<AutomationLaneInfo> lanes{laneOver(ControlTarget::trackVolume(1))};
    const std::vector<ControlTarget> bound{ControlTarget::modParam(project.path, 1, 0)};

    const auto addressed = addressedIn({project.track}, lanes, bound);

    CHECK(addressed.numDevices() == 0);
}

TEST_CASE("Devices outside the FX tree are addressed by their own path",
          "[core][parameters][addressed]") {
    auto track = makeTrack();
    track.chain.postFxChainElements.push_back({makeDevice(8)});
    track.chain.mixerAnalysisElements.push_back({makeDevice(9)});

    const auto postFx = ChainNodePath::postFxDevice(track.id, 8);
    const auto analysis = ChainNodePath::mixerAnalysisDevice(track.id, 9);

    const std::vector<ControlTarget> bound{ControlTarget::pluginParam(postFx, 1),
                                           ControlTarget::pluginParam(analysis, 2)};

    const auto addressed = addressedIn({track}, {}, bound);

    CHECK(addressed.addresses(postFx, 1));
    CHECK(addressed.addresses(analysis, 2));
}

TEST_CASE("The master track's devices are addressed too", "[core][parameters][addressed]") {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;

    auto device = makeDevice(11);
    device.visibleParameters = {1};
    const auto path = chain_walk::deviceIn(ChainNodePath::trackLevel(master.id), device.id);
    master.chain.fxChainElements.push_back(makeDeviceElement(std::move(device)));

    AddressingSources sources;
    sources.master = &master;

    const auto addressed = AddressedParameters::from(sources);

    CHECK(addressed.addresses(path, 1));
}
