#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>

#include "magda/daw/audio/plugins/DeviceManifests.hpp"

// The manifests the build writes (#2939), against the frozen parameter order (#1887). A device
// that renumbers, renames an id or drops a parameter fails here, before a saved project does.

namespace {

namespace audio = magda::daw::audio;
namespace sdk = magda::sdk;

struct FrozenDevice {
    std::vector<juce::String> ids;
};

std::map<juce::String, FrozenDevice> frozenSchema() {
    std::map<juce::String, FrozenDevice> devices;
    juce::StringArray lines;
    lines.addLines(juce::File(MAGDA_DEVICE_PARAM_SCHEMA_FILE).loadFileAsString());
    for (const auto& line : lines) {
        if (line.isEmpty())
            continue;
        const auto id = line.upToFirstOccurrenceOf(" ", false, false);
        const auto rest = line.fromFirstOccurrenceOf(" ", false, false);
        FrozenDevice device;
        if (rest.containsChar(' '))
            for (const auto& parameter : juce::StringArray::fromTokens(
                     rest.fromFirstOccurrenceOf(" ", false, false), ",", ""))
                device.ids.push_back(parameter);
        devices[id] = std::move(device);
    }
    return devices;
}

}  // namespace

TEST_CASE("Every device manifest keeps the frozen ids and indices", "[devices][manifest][2939]") {
    const auto frozen = frozenSchema();
    REQUIRE_FALSE(frozen.empty());

    const auto entries = audio::buildBasePackManifests();
    int manifests = 0;
    std::set<juce::String> accounted;
    for (const auto& entry : entries) {
        INFO(entry.pluginId);
        accounted.insert(entry.pluginId);
        if (!entry.manifest) {
            CHECK(entry.skipReason.isNotEmpty());
            continue;
        }

        const auto found = frozen.find(entry.pluginId);
        REQUIRE(found != frozen.end());
        const auto& ids = found->second.ids;
        const auto& parameters = entry.manifest->parameters;

        REQUIRE(parameters.size() == ids.size());
        for (std::size_t slot = 0; slot < parameters.size(); ++slot) {
            INFO("slot " << slot);
            CHECK(juce::String(parameters[slot].stableId) == ids[slot]);
            CHECK(parameters[slot].index == static_cast<int>(slot));
        }
        ++manifests;
    }
    CHECK(manifests > 40);

    // A frozen device with parameters is written, skipped on the record, or one of the engine
    // effects and 4OSC the catalog does not register, which no SDK device stands behind.
    const std::set<juce::String> engineHosted{"4bandEq", "4osc",         "compressor", "delay",
                                              "lowpass", "pitchShifter", "reverb",     "volume"};
    for (const auto& [pluginId, device] : frozen) {
        if (!device.ids.empty()) {
            INFO(pluginId);
            CHECK((accounted.contains(pluginId) || engineHosted.contains(pluginId)));
        }
    }
}

TEST_CASE("Every device manifest writes, reads back and writes the same text",
          "[devices][manifest][2939]") {
    for (const auto& entry : audio::buildBasePackManifests()) {
        if (!entry.manifest)
            continue;
        INFO(entry.pluginId);

        std::string error;
        const auto text = sdk::writeManifest(*entry.manifest, error);
        INFO(error);
        REQUIRE(text.has_value());

        const auto read = sdk::readManifest(*text, error);
        INFO(error);
        REQUIRE(read.has_value());
        CHECK(read->deviceType == entry.manifest->deviceType);
        CHECK(read->parameters.size() == entry.manifest->parameters.size());

        const auto again = sdk::writeManifest(*read, error);
        REQUIRE(again.has_value());
        CHECK(*again == *text);
    }
}

TEST_CASE("Base-pack devices start at version 1 and runtime Faust devices take their parameters "
          "from state",
          "[devices][manifest][2939]") {
    int fromState = 0;
    for (const auto& entry : audio::buildBasePackManifests()) {
        if (!entry.manifest)
            continue;
        INFO(entry.pluginId);
        CHECK(entry.manifest->deviceVersion == 1);

        const bool runtimeFaust =
            entry.pluginId == "faust-fx" || entry.pluginId == "faust-instrument";
        CHECK((entry.manifest->parameterSource == sdk::ParameterSource::State) == runtimeFaust);
        fromState += runtimeFaust ? 1 : 0;
    }
    CHECK(fromState == 2);
}
