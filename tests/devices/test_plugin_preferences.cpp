#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/PluginPreferences.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

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

struct DataDirScope {
    DataDirScope() {
        if (const char* value = std::getenv("MAGDA_DATA_DIR"); value != nullptr)
            previousDataDir = juce::String::fromUTF8(value);

        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("magda-plugin-preferences-test")
                      .getNonexistentChildFile("data", "");
        tempDir.createDirectory();
        setEnv("MAGDA_DATA_DIR", tempDir.getFullPathName().toRawUTF8());
        magda::Config::getInstance().setDataDir({});
        magda::paths::resolve();
        magda::paths::pluginPreferencesFile().deleteFile();
    }

    ~DataDirScope() {
        if (previousDataDir.isEmpty())
            unsetEnv("MAGDA_DATA_DIR");
        else
            setEnv("MAGDA_DATA_DIR", previousDataDir.toRawUTF8());
        magda::Config::getInstance().setDataDir({});
        magda::paths::resolve();
        tempDir.deleteRecursively();
    }

    juce::String previousDataDir;
    juce::File tempDir;
};

}  // namespace

TEST_CASE("PluginPreferences uses model device identity", "[plugin_preferences]") {
    magda::DeviceInfo external;
    external.pluginId = "FallbackPluginId";
    external.uniqueId = "VST3-Addictive Drums 2-9023849b-5bcaaf9b";
    REQUIRE(magda::PluginPreferences::identifierForDevice(external) == external.uniqueId);

    magda::DeviceInfo internal;
    internal.pluginId = "4osc";
    REQUIRE(magda::PluginPreferences::identifierForDevice(internal) == "4osc");
}

TEST_CASE("PluginPreferences ignores Tracktion instrument wrapper id", "[plugin_preferences]") {
    DataDirScope dataDir;
    auto& prefs = magda::PluginPreferences::getInstance();

    REQUIRE_FALSE(prefs.prefersDrumGrid("rack"));
    REQUIRE(prefs.prefersDrumGrid("drumgrid"));
    REQUIRE_FALSE(prefs.treatsAsMidiFx("rack"));

    prefs.setTreatsAsMidiFx("VST3-Stochas-9e4b6434-3fb7fec3", true);
    REQUIRE(prefs.treatsAsMidiFx("VST3-Stochas-9e4b6434-3fb7fec3"));

    prefs.setTreatsAsMidiFx("VST3-Stochas-9e4b6434-3fb7fec3", false);
    REQUIRE_FALSE(prefs.treatsAsMidiFx("VST3-Stochas-9e4b6434-3fb7fec3"));
}

TEST_CASE("PluginPreferences stores browser category overrides separately",
          "[plugin_preferences]") {
    DataDirScope dataDir;
    auto& prefs = magda::PluginPreferences::getInstance();
    const juce::String pluginId = "VST3-Stochas-category-override-test";

    prefs.setBrowserCategoryOverride(pluginId, " MIDI FX ");
    REQUIRE(prefs.browserCategoryOverride(pluginId) == "MIDI FX");
    REQUIRE(prefs.treatsAsMidiFx(pluginId));

    prefs.setBrowserCategoryOverride(pluginId, "Analyzer");
    REQUIRE(prefs.browserCategoryOverride(pluginId) == "Analyzer");
    REQUIRE_FALSE(prefs.treatsAsMidiFx(pluginId));

    prefs.setBrowserCategoryOverride(pluginId, {});
    REQUIRE(prefs.browserCategoryOverride(pluginId).isEmpty());
}

TEST_CASE("Plugin drag conversion preserves raw category facts with browser override",
          "[plugin_preferences]") {
    auto* obj = new juce::DynamicObject();
    juce::var payload(obj);
    obj->setProperty("name", "Stochas");
    obj->setProperty("manufacturer", "Surge Synth Team");
    obj->setProperty("uniqueId", "VST3-Stochas-drag-category-test");
    obj->setProperty("fileOrIdentifier", "/Library/Audio/Plug-Ins/VST3/Stochas.vst3");
    obj->setProperty("format", "VST3");
    obj->setProperty("category", "Effect");
    obj->setProperty("subcategory", "MIDI");
    obj->setProperty("rawCategory", "Instrument");
    obj->setProperty("rawSubcategory", "Synth");
    obj->setProperty("categoryOverride", "MIDI FX");

    auto device = magda::TrackManager::deviceInfoFromPluginObject(*obj);

    REQUIRE(device.isInstrument);
    REQUIRE(device.deviceType == magda::DeviceType::Instrument);
    REQUIRE(device.browserCategoryOverride == "MIDI FX");
}

TEST_CASE("Device serialization keeps browser category override separate from technical type",
          "[plugin_preferences]") {
    magda::DeviceInfo device;
    device.id = 42;
    device.name = "Stochas";
    device.pluginId = "VST3-Stochas-serialization-category-test";
    device.uniqueId = device.pluginId;
    device.isInstrument = true;
    device.deviceType = magda::DeviceType::Instrument;
    device.browserCategoryOverride = "MIDI FX";

    auto json = magda::ProjectSerializer::serializeDeviceInfo(device);
    magda::DeviceInfo restored;
    REQUIRE(magda::ProjectSerializer::deserializeDeviceInfo(json, restored));

    REQUIRE(restored.isInstrument);
    REQUIRE(restored.deviceType == magda::DeviceType::Instrument);
    REQUIRE(restored.browserCategoryOverride == "MIDI FX");
}
