#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/audio/plugins/MagdaDevice.hpp"
#include "magda/daw/engine/DeviceParameterScan.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

namespace {

const magda::ScannedPluginParameter* findByName(
    const std::vector<magda::ScannedPluginParameter>& scanned, const juce::String& name) {
    const auto match = std::ranges::find(scanned, name, &magda::ScannedPluginParameter::name);
    return match != scanned.end() ? &*match : nullptr;
}

namespace audio = magda::daw::audio;

constexpr const char* kScanTestDeviceId = "test-scan-device";

/// A device whose parameters are chosen to exercise the scan rather than to
/// make a sound: an anchor below zero, no anchor at all, and no name.
class ScanTestDevice : public audio::MagdaDevice {
  public:
    audio::DeviceProperties properties() const override {
        audio::DeviceProperties properties;
        properties.pluginId = kScanTestDeviceId;
        properties.name = "Scan Test Device";
        return properties;
    }

    void process(audio::DeviceProcessContext&) override {}

    int parameterCount() const override {
        return 3;
    }

    magda::ParameterInfo parameterInfo(int index) const override {
        magda::ParameterInfo info;
        switch (index) {
            case 0:
                info.stableId = "threshold";
                info.name = "Threshold";
                info.unit = "dB";
                info.minValue = -60.0f;
                info.maxValue = 6.0f;
                info.scaleAnchor = -12.0f;
                break;
            case 1:
                info.stableId = "mix";
                info.name = "Mix";
                info.unit = "%";
                info.minValue = 0.0f;
                info.maxValue = 100.0f;
                break;
            default:
                // Nameless, and so not offered for configuration.
                info.stableId = "reserved";
                break;
        }
        return info;
    }
};

void registerScanTestPack(audio::InternalPluginRegistry& registry) {
    audio::InternalPluginSpec spec;
    spec.pluginId = kScanTestDeviceId;
    spec.displayName = "Scan Test Device";
    spec.createDevice =
        [](const audio::DevicePluginCreationContext&) -> std::unique_ptr<audio::MagdaDevice> {
        return std::make_unique<ScanTestDevice>();
    };
    registry.registerPlugin(spec);
}

const bool scanTestPackRegistered = audio::registerDevicePack(registerScanTestPack);

}  // namespace

TEST_CASE("A MAGDA device is scanned off the catalog, with no engine",
          "[device-parameter-scan][2601]") {
    // Nothing here starts an engine or opens an Edit: the Configure Parameters
    // dialog used to need the fork's Edit for this, and the native engine has
    // none, so every MAGDA device fell back to mock parameters there.
    const auto scanned = magda::scanDeviceParameters("arpeggiator");
    REQUIRE_FALSE(scanned.empty());

    const auto* octaves = findByName(scanned, "Octaves");
    REQUIRE(octaves != nullptr);
    CHECK(octaves->stableId == "octaves");

    // The device's own range, not the 0..1 the host wraps every one of its
    // parameters in -- which is what a scan through the fork's automatable
    // list reported.
    CHECK(octaves->rangeMin == 1.0f);
    CHECK(octaves->rangeMax == 4.0f);
}

TEST_CASE("A discrete parameter is scanned with its choices", "[device-parameter-scan][2601]") {
    const auto scanned = magda::scanDeviceParameters("arpeggiator");
    const auto* pattern = findByName(scanned, "Pattern");
    REQUIRE(pattern != nullptr);

    CHECK(pattern->scale == magda::ParameterScale::Discrete);
    CHECK(pattern->valueTable ==
          std::vector<juce::String>{"Up", "Down", "Up/Down", "Down/Up", "Random", "As Played"});
    CHECK(pattern->scanInput.stateCount == 6);
    CHECK(pattern->scanInput.displayTexts == pattern->valueTable);
}

TEST_CASE("A scan result is addressed by its position in the list",
          "[device-parameter-scan][2601]") {
    // What a detection result is applied by, so the two have to agree.
    const auto scanned = magda::scanDeviceParameters("arpeggiator");
    REQUIRE_FALSE(scanned.empty());

    for (std::size_t at = 0; at < scanned.size(); ++at) {
        CHECK(scanned[at].scanInput.paramIndex == static_cast<int>(at));
        CHECK(scanned[at].scanInput.name == scanned[at].name);
    }
}

TEST_CASE("An id no registered device answers to scans as nothing",
          "[device-parameter-scan][2601]") {
    // An external plugin's id reaches here too, and is not this function's to
    // answer: the caller falls back to opening the plugin.
    CHECK(magda::scanDeviceParameters("VST3-Surge-XT-1a2b3c4d").empty());
    CHECK(magda::scanDeviceParameters({}).empty());
}

TEST_CASE("The internal scan needs no Edit to build a plugin in", "[device-parameter-scan][2601]") {
    // What the native engine has: the fork's services and no Edit of its own.
    // Until the device answered for itself this branch returned nothing, and
    // Configure Parameters fell back to a mock list for every MAGDA device.
    magda::TracktionEngineWrapper wrapper;
    const auto scanned = wrapper.scanPluginParameters("arpeggiator", true);

    REQUIRE_FALSE(scanned.empty());
    CHECK(scanned.size() == magda::scanDeviceParameters("arpeggiator").size());
}

TEST_CASE("A scale anchor below zero is the scanned centre", "[device-parameter-scan][2601]") {
    // The anchor is a real value, and a range that straddles zero has real
    // values on both sides of it: a sign test would drop this one and report
    // the midpoint, -27 dB, as the parameter's centre.
    REQUIRE(scanTestPackRegistered);
    const auto scanned = magda::scanDeviceParameters(kScanTestDeviceId);

    const auto* threshold = findByName(scanned, "Threshold");
    REQUIRE(threshold != nullptr);
    CHECK(threshold->rangeCenter == -12.0f);
}

TEST_CASE("A parameter that declares no anchor takes its midpoint",
          "[device-parameter-scan][2601]") {
    const auto scanned = magda::scanDeviceParameters(kScanTestDeviceId);

    const auto* mix = findByName(scanned, "Mix");
    REQUIRE(mix != nullptr);
    CHECK(mix->rangeCenter == 50.0f);
}

TEST_CASE("A nameless parameter is left out without shifting the rest",
          "[device-parameter-scan][2601]") {
    const auto scanned = magda::scanDeviceParameters(kScanTestDeviceId);

    REQUIRE(scanned.size() == 2);
    for (std::size_t at = 0; at < scanned.size(); ++at)
        CHECK(scanned[at].scanInput.paramIndex == static_cast<int>(at));
}
