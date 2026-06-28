#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/PluginPreferences.hpp"

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
}
