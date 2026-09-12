#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/plugins/ArpeggiatorPlugin.hpp"
#include "magda/daw/audio/plugins/DeviceCatalogParameters.hpp"
#include "magda/daw/audio/plugins/DeviceStateHydration.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/ChainWalk.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
namespace ds = magda::device_state;
namespace hydration = magda::daw::audio::device_state_hydration;

// ============================================================================
// #2613 - the model carries a MAGDA device's parameters whichever engine
// renders. Only the fork ever filled the array, off a plugin in its Edit, so a
// device added under the native engine had none and every write to one was
// dropped for want of an entry to land on.
// ============================================================================

namespace {

using magda::daw::audio::seedDeclaredParameters;

DeviceInfo internalDevice(const juce::String& pluginId) {
    DeviceInfo device;
    device.id = 1;
    device.format = PluginFormat::Internal;
    device.pluginId = pluginId;
    return device;
}

const char* kPolySynth = magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin::xmlTypeName;

}  // namespace

TEST_CASE("Seeding gives the model every parameter a device declares", "[device-catalog-params]") {
    auto device = internalDevice(kPolySynth);
    REQUIRE(device.parameters.empty());

    REQUIRE(seedDeclaredParameters(device));
    REQUIRE_FALSE(device.parameters.empty());

    // Addressed by the index the device declared, and carrying the metadata the
    // plan's value layer converts through.
    for (int index = 0; index < static_cast<int>(device.parameters.size()); ++index) {
        const auto* param = device.findParameterByIndex(index);
        REQUIRE(param != nullptr);
        CHECK(param->name.isNotEmpty());
        CHECK(param->currentValue == Catch::Approx(param->defaultValue));
    }
}

TEST_CASE("Seeding leaves a parameter the model already carries alone", "[device-catalog-params]") {
    auto device = internalDevice(kPolySynth);

    ParameterInfo edited;
    edited.paramIndex = 0;
    edited.name = "Osc 1 Wave";
    edited.currentValue = 2.0f;
    device.parameters.push_back(edited);

    REQUIRE(seedDeclaredParameters(device));

    const auto* kept = device.findParameterByIndex(0);
    REQUIRE(kept != nullptr);
    CHECK(kept->currentValue == Catch::Approx(2.0f));
    CHECK(device.parameters.size() > 1);

    // And a second pass finds nothing left to add.
    CHECK_FALSE(seedDeclaredParameters(device));
}

TEST_CASE("Seeding is not for a plugin somebody else shipped", "[device-catalog-params]") {
    auto device = internalDevice(kPolySynth);
    device.format = PluginFormat::VST3;

    CHECK_FALSE(seedDeclaredParameters(device));
    CHECK(device.parameters.empty());
}

TEST_CASE("A saved value survives the seed that completes its array", "[device-catalog-params]") {
    // Hydration first, seeding second: the other order would make the saved
    // value look like one the model already had, and it would be dropped.
    auto device = internalDevice(daw::audio::ArpeggiatorPlugin::xmlTypeName);

    ds::Doc doc;
    doc.deviceType = device.pluginId;
    doc.paramsAreDisplayDomain = true;
    doc.params.push_back({daw::audio::ArpeggiatorPlugin::kGate, "gate", 0.8f});
    device.pluginState = ds::encode(doc);

    hydration::completeDeviceParameters(device);

    const auto* gate = device.findParameterByIndex(daw::audio::ArpeggiatorPlugin::kGate);
    REQUIRE(gate != nullptr);
    CHECK(gate->currentValue == Catch::Approx(0.8f));
    CHECK(device.parameters.size() > 1);
}

TEST_CASE("A device added to a track takes a parameter write",
          "[device-catalog-params][.singleton]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = tracks.createTrack("Synth");

    DeviceInfo synth;
    synth.name = "Poly Synth";
    synth.pluginId = kPolySynth;
    synth.format = PluginFormat::Internal;
    const auto deviceId = tracks.addDeviceToTrack(trackId, synth);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const auto path = chain_walk::deviceIn(ChainNodePath::trackLevel(trackId), deviceId);
    const auto* placed = tracks.getDeviceInChainByPath(path);
    REQUIRE(placed != nullptr);
    REQUIRE_FALSE(placed->parameters.empty());

    const auto* before = placed->findParameterByIndex(0);
    REQUIRE(before != nullptr);
    const auto moved = before->currentValue + 1.0f;

    tracks.setDeviceParameterValue(path, 0, moved);
    CHECK(tracks.getDeviceInChainByPath(path)->findParameterByIndex(0)->currentValue ==
          Catch::Approx(moved));
}
