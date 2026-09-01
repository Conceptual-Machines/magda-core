#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/PluginParameterConfigStore.hpp"

namespace store = magda::PluginParameterConfigStore;

namespace {

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/// Points paths::dataDir at a throwaway directory for the test's lifetime, so
/// the store never touches the real per-user PluginConfigs.
struct TempDataDir {
    TempDataDir() {
        if (const char* value = std::getenv("MAGDA_DATA_DIR"); value != nullptr) {
            previous = juce::String::fromUTF8(value);
            hadPrevious = true;
        }
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("magda-param-config-store-test");
        dir.deleteRecursively();
        dir.createDirectory();
        setEnv("MAGDA_DATA_DIR", dir.getFullPathName().toRawUTF8());
        magda::paths::resolve();
    }
    ~TempDataDir() {
        if (hadPrevious)
            setEnv("MAGDA_DATA_DIR", previous.toRawUTF8());
        else
            unsetEnv("MAGDA_DATA_DIR");
        magda::paths::resolve();
        dir.deleteRecursively();
    }

    juce::File dir;
    juce::String previous;
    bool hadPrevious = false;
};

magda::DeviceInfo makeExternalDevice() {
    magda::DeviceInfo device;
    device.uniqueId = "VST3-Surge-XT-1a2b3c4d";
    device.format = magda::PluginFormat::VST3;
    device.parameters.emplace_back(0, "Cutoff", "Hz", 20.0f, 20000.0f, 800.0f);
    device.parameters.emplace_back(1, "Resonance", "%", 0.0f, 100.0f, 10.0f);
    device.parameters.emplace_back(2, "Mode", "", 0.0f, 2.0f, 0.0f,
                                   magda::ParameterScale::Discrete);
    device.parameters[2].choices = {"LP", "BP", "HP"};
    return device;
}

}  // namespace

TEST_CASE("parameter config save and load round-trip", "[param-config-store]") {
    TempDataDir temp;
    const auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    config.entries[0].visible = true;
    config.entries[1].visible = true;
    config.entries[1].aiAgent = true;
    config.entries[2].miniMixer = true;
    config.aiPrompt = "Warm analog pads";
    REQUIRE(store::save(device.uniqueId, config));

    const auto loaded = store::load(device.uniqueId);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->pluginId == device.uniqueId);
    REQUIRE(loaded->aiPrompt == "Warm analog pads");
    REQUIRE(loaded->entries.size() == 3);

    REQUIRE(loaded->entries[0].visible);
    REQUIRE_FALSE(loaded->entries[0].aiAgent);
    REQUIRE(loaded->entries[1].aiAgent);
    REQUIRE(loaded->entries[2].miniMixer);

    // Detection data survives the trip.
    REQUIRE(loaded->entries[0].name == "Cutoff");
    REQUIRE(loaded->entries[0].unit.has_value());
    REQUIRE(*loaded->entries[0].unit == "Hz");
    REQUIRE(loaded->entries[0].rangeMin.has_value());
    REQUIRE(*loaded->entries[0].rangeMin == 20.0f);
    REQUIRE(loaded->entries[2].scale.has_value());
    REQUIRE(*loaded->entries[2].scale == magda::ParameterScale::Discrete);
    REQUIRE(loaded->entries[2].choices.has_value());
    REQUIRE(*loaded->entries[2].choices == std::vector<juce::String>{"LP", "BP", "HP"});
}

TEST_CASE("fromDevice describes every parameter with everything off", "[param-config-store]") {
    const auto device = makeExternalDevice();
    const auto config = store::fromDevice(device);

    REQUIRE(config.pluginId == device.uniqueId);
    REQUIRE(config.entries.size() == device.parameters.size());
    for (size_t i = 0; i < config.entries.size(); ++i) {
        REQUIRE(config.entries[i].index == static_cast<int>(i));
        REQUIRE(config.entries[i].name == device.parameters[i].name);
        REQUIRE_FALSE(config.entries[i].visible);
        REQUIRE_FALSE(config.entries[i].miniMixer);
        REQUIRE_FALSE(config.entries[i].aiAgent);
    }
}

TEST_CASE("applyToDevice rebuilds selections and applies overrides", "[param-config-store]") {
    TempDataDir temp;
    auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    config.entries[0].visible = true;
    config.entries[0].aiAgent = true;
    config.entries[0].unit = "kHz";
    config.entries[0].rangeMax = 20.0f;
    config.entries[1].miniMixer = true;
    config.aiPrompt = "Bright leads";
    REQUIRE(store::save(device.uniqueId, config));

    // Stale state on the device must be replaced, not merged.
    device.visibleParameters = {2};
    device.aiSoundDesignerParameters = {2};
    REQUIRE(store::applyToDevice(device.uniqueId, device));

    REQUIRE(device.visibleParameters == std::vector<int>{0});
    REQUIRE(device.miniMixerParameters == std::vector<int>{1});
    REQUIRE(device.aiSoundDesignerParameters == std::vector<int>{0});
    REQUIRE(device.aiSoundDesignerPrompt == "Bright leads");
    REQUIRE(device.parameters[0].unit == "kHz");
    REQUIRE(device.parameters[0].maxValue == 20.0f);

    // No config for this id: the device is left alone.
    magda::DeviceInfo other = makeExternalDevice();
    REQUIRE_FALSE(store::applyToDevice("VST3-Not-Configured", other));
}

TEST_CASE("legacy visible-only config files still load", "[param-config-store]") {
    TempDataDir temp;
    const juce::String uniqueId = "VST3-Legacy-Plugin";

    juce::XmlElement root("ParameterConfig");
    root.setAttribute("pluginId", uniqueId);
    auto* visible = root.createNewChildElement("VisibleParameters");
    visible->createNewChildElement("Param")->setAttribute("index", 1);
    visible->createNewChildElement("Param")->setAttribute("index", 4);
    magda::paths::pluginConfigsDir().createDirectory();
    REQUIRE(root.writeTo(store::configFileFor(uniqueId)));

    const auto loaded = store::load(uniqueId);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->entries.size() == 2);
    REQUIRE(loaded->entries[0].index == 1);
    REQUIRE(loaded->entries[0].visible);
    REQUIRE_FALSE(loaded->entries[0].aiAgent);
    // Legacy entries carry no detection overrides.
    REQUIRE_FALSE(loaded->entries[0].unit.has_value());
    REQUIRE_FALSE(loaded->entries[0].scale.has_value());

    REQUIRE_FALSE(store::hasAiSoundDesignerParameters(uniqueId));
}

TEST_CASE("hasAiSoundDesignerParameters reflects the saved AI selection", "[param-config-store]") {
    TempDataDir temp;
    const auto device = makeExternalDevice();

    auto config = store::fromDevice(device);
    REQUIRE(store::save(device.uniqueId, config));
    REQUIRE_FALSE(store::hasAiSoundDesignerParameters(device.uniqueId));

    config.entries[1].aiAgent = true;
    REQUIRE(store::save(device.uniqueId, config));
    REQUIRE(store::hasAiSoundDesignerParameters(device.uniqueId));
}
