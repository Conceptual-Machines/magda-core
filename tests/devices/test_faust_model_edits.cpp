// A Faust patch load lands on the model: its source and the controls it declares (#2659).
//
// No audio engine in this binary, so TrackManager skips the projection and what is under test is
// the model a save writes and both engines build from.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/faust/FaustModelEdits.hpp"
#include "magda/daw/audio/plugins/FaustInstrumentPlugin.hpp"
#include "magda/daw/audio/plugins/FaustPlugin.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;
using Catch::Approx;

namespace {

// Each names "stdfaust.lib" so the compile does not import the library. A control holds its slot
// through [idx:N]; untagged ones take slots in the order Faust reports them.
constexpr const char* kGainPatch = R"FAUST(// stdfaust.lib
process = *(hslider("Gain[idx:0]", 0.5, 0, 2, 0.01));
)FAUST";

constexpr const char* kFilterPatch = R"FAUST(// stdfaust.lib
process = *(hslider("Gain[idx:0]", 0.5, 0, 4, 0.01)) : *(hslider("Cutoff[idx:1]", 1000, 20, 20000, 1));
)FAUST";

ChainNodePath addFaust(const char* pluginId) {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Faust", TrackType::Media);

    DeviceInfo device;
    device.name = "Faust";
    device.pluginId = pluginId;
    device.format = PluginFormat::Internal;

    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

const DeviceInfo& modelDevice(const ChainNodePath& path) {
    const auto* device = TrackManager::getInstance().getDeviceInChainByPath(path);
    REQUIRE(device != nullptr);
    return *device;
}

const ParameterInfo* parameterNamed(const ChainNodePath& path, const juce::String& name) {
    for (const auto& parameter : modelDevice(path).parameters)
        if (parameter.name == name)
            return &parameter;
    return nullptr;
}

}  // namespace

TEST_CASE("A Faust patch load puts its source and its controls on the model",
          "[faust][devices][2659]") {
    const auto path = addFaust(daw::audio::FaustPlugin::xmlTypeName);

    juce::String error;
    REQUIRE(faust_edits::loadSource(path, "Gain", kGainPatch, error));

    const auto doc = device_state::decode(modelDevice(path).pluginState);
    REQUIRE(doc.has_value());
    CHECK(doc->root.props[daw::audio::kFaustDspSourceProperty].toString() == kGainPatch);
    CHECK(doc->root.props[daw::audio::kFaustDspNameProperty].toString() == "Gain");

    // The patch's one control and nothing for the pool's empty slots.
    REQUIRE(modelDevice(path).parameters.size() == 1);
    const auto* gain = parameterNamed(path, "Gain");
    REQUIRE(gain != nullptr);
    CHECK(gain->maxValue == Approx(2.0f));
    CHECK(gain->currentValue == Approx(0.5f));
}

TEST_CASE("A control the next patch still has keeps its value, and a new one starts at its default",
          "[faust][devices][2659]") {
    const auto path = addFaust(daw::audio::FaustPlugin::xmlTypeName);

    juce::String error;
    REQUIRE(faust_edits::loadSource(path, "Gain", kGainPatch, error));
    const auto* gain = parameterNamed(path, "Gain");
    REQUIRE(gain != nullptr);
    TrackManager::getInstance().setDeviceParameterValue(path, gain->paramIndex, 1.5f);

    REQUIRE(faust_edits::loadSource(path, "Filter", kFilterPatch, error));
    REQUIRE(modelDevice(path).parameters.size() == 2);

    gain = parameterNamed(path, "Gain");
    REQUIRE(gain != nullptr);
    CHECK(gain->maxValue == Approx(4.0f));
    CHECK(gain->currentValue == Approx(1.5f));

    const auto* cutoff = parameterNamed(path, "Cutoff");
    REQUIRE(cutoff != nullptr);
    CHECK(cutoff->currentValue == Approx(1000.0f));
}

TEST_CASE("A patch that does not compile leaves the model as it was", "[faust][devices][2659]") {
    const auto path = addFaust(daw::audio::FaustPlugin::xmlTypeName);

    juce::String error;
    REQUIRE(faust_edits::loadSource(path, "Gain", kGainPatch, error));
    const auto before = modelDevice(path);

    CHECK_FALSE(faust_edits::loadSource(path, "Broken", "process = ;", error));
    CHECK(error.isNotEmpty());
    CHECK(modelDevice(path).pluginState == before.pluginState);
    CHECK(modelDevice(path).parameters.size() == before.parameters.size());
}

TEST_CASE("A Faust instrument keeps its voice settings across a patch load",
          "[faust][devices][2659]") {
    const auto path = addFaust(daw::audio::FaustInstrumentPlugin::xmlTypeName);

    juce::String error;
    REQUIRE(faust_edits::loadSource(path, "Gain", kGainPatch, error));
    const auto* glide = parameterNamed(path, "Glide");
    REQUIRE(glide != nullptr);
    TrackManager::getInstance().setDeviceParameterValue(path, glide->paramIndex, 250.0f);

    REQUIRE(faust_edits::loadSource(path, "Filter", kFilterPatch, error));
    glide = parameterNamed(path, "Glide");
    REQUIRE(glide != nullptr);
    CHECK(glide->currentValue == Approx(250.0f));
    CHECK(parameterNamed(path, "Voice Mode") != nullptr);
}

TEST_CASE("A patch load refuses a device that is not a runtime Faust device",
          "[faust][devices][2659]") {
    const auto path = addFaust("magda_convolution");

    juce::String error;
    CHECK_FALSE(faust_edits::loadSource(path, "Gain", kGainPatch, error));
    CHECK(error.isNotEmpty());
}
