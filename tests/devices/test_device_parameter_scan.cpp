#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/DeviceParameterScan.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

namespace {

const magda::ScannedPluginParameter* findByName(
    const std::vector<magda::ScannedPluginParameter>& scanned, const juce::String& name) {
    const auto match = std::ranges::find(scanned, name, &magda::ScannedPluginParameter::name);
    return match != scanned.end() ? &*match : nullptr;
}

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
