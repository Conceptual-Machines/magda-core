#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"

namespace paths = magda::paths;

namespace {

// Setenv helpers — cross-platform-ish; portable tests.
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

struct EnvScope {
    EnvScope() {
        // Snapshot the three knobs and restore on dtor so tests don't leak.
        snapshot("MAGDA_DATA_DIR", dataPrev);
        snapshot("MAGDA_PRESETS_DIR", presetsPrev);
        snapshot("MAGDA_RENDER_DIR", renderPrev);
    }
    ~EnvScope() {
        restore("MAGDA_DATA_DIR", dataPrev);
        restore("MAGDA_PRESETS_DIR", presetsPrev);
        restore("MAGDA_RENDER_DIR", renderPrev);
        // Also reset Config overrides so the next test starts clean.
        magda::Config::getInstance().setDataDir({});
        magda::Config::getInstance().setPresetsDir({});
        magda::Config::getInstance().setRenderFolder({});
        paths::resolve();
    }

    static void snapshot(const char* name, juce::String& out) {
        if (const char* v = std::getenv(name); v != nullptr)
            out = juce::String::fromUTF8(v);
    }
    static void restore(const char* name, const juce::String& prev) {
        if (prev.isEmpty())
            unsetEnv(name);
        else
            setEnv(name, prev.toRawUTF8());
    }

    juce::String dataPrev;
    juce::String presetsPrev;
    juce::String renderPrev;
};

}  // namespace

TEST_CASE("paths::dataDir defaults to OS appdata/MAGDA", "[app_paths]") {
    EnvScope scope;
    unsetEnv("MAGDA_DATA_DIR");
    magda::Config::getInstance().setDataDir({});
    paths::resolve();

    auto expected = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("MAGDA");
    REQUIRE(paths::dataDir() == expected);
    REQUIRE_FALSE(paths::dataDirOverriddenByEnv());
}

TEST_CASE("paths::dataDir env var beats default", "[app_paths]") {
    EnvScope scope;
    setEnv("MAGDA_DATA_DIR", "/tmp/magda-env-test/data");
    paths::resolve();

    REQUIRE(paths::dataDir() == juce::File("/tmp/magda-env-test/data"));
    REQUIRE(paths::dataDirOverriddenByEnv());
}

TEST_CASE("paths::dataDir env var beats Config", "[app_paths]") {
    EnvScope scope;
    setEnv("MAGDA_DATA_DIR", "/tmp/magda-env-wins/data");
    magda::Config::getInstance().setDataDir("/tmp/magda-config-loses/data");
    paths::resolve();

    REQUIRE(paths::dataDir() == juce::File("/tmp/magda-env-wins/data"));
    REQUIRE(paths::dataDirOverriddenByEnv());
}

TEST_CASE("paths::dataDir Config beats default", "[app_paths]") {
    EnvScope scope;
    unsetEnv("MAGDA_DATA_DIR");
    magda::Config::getInstance().setDataDir("/tmp/magda-config-test/data");
    paths::resolve();

    REQUIRE(paths::dataDir() == juce::File("/tmp/magda-config-test/data"));
    REQUIRE_FALSE(paths::dataDirOverriddenByEnv());
}

TEST_CASE("paths subpaths compose under dataDir", "[app_paths]") {
    EnvScope scope;
    setEnv("MAGDA_DATA_DIR", "/tmp/magda-subpath-test");
    paths::resolve();

    REQUIRE(paths::logsDir() == juce::File("/tmp/magda-subpath-test/Logs"));
    REQUIRE(paths::controllerScriptsDir() ==
            juce::File("/tmp/magda-subpath-test/Scripts/Controllers"));
    REQUIRE(paths::controllerProfilesDir() == juce::File("/tmp/magda-subpath-test/controllers"));
    REQUIRE(paths::pluginConfigsDir() == juce::File("/tmp/magda-subpath-test/PluginConfigs"));
    REQUIRE(paths::pluginListFile() == juce::File("/tmp/magda-subpath-test/PluginList.xml"));
    REQUIRE(paths::pluginExclusionsFile() ==
            juce::File("/tmp/magda-subpath-test/plugin_exclusions.txt"));
    REQUIRE(paths::lastScanReportFile() ==
            juce::File("/tmp/magda-subpath-test/last_scan_report.txt"));
    REQUIRE(paths::pluginFavoritesFile() ==
            juce::File("/tmp/magda-subpath-test/plugin_favorites.xml"));
    REQUIRE(paths::pluginAliasesFile() == juce::File("/tmp/magda-subpath-test/plugin_aliases.xml"));
    REQUIRE(paths::pluginScanMarkerFile("VST3") ==
            juce::File("/tmp/magda-subpath-test/scanning_VST3.txt"));
    REQUIRE(paths::parameterDetectorLog() ==
            juce::File("/tmp/magda-subpath-test/param_detector.log"));
}

TEST_CASE("paths::configFile is anchored to OS default regardless of override", "[app_paths]") {
    EnvScope scope;
    setEnv("MAGDA_DATA_DIR", "/tmp/magda-anchor-test/data");
    paths::resolve();

    auto expected = paths::alwaysOSDefault().getChildFile("config.json");
    REQUIRE(paths::configFile() == expected);
    REQUIRE_FALSE(paths::configFile().getFullPathName().contains("magda-anchor-test"));
}

TEST_CASE("paths::resolve picks up Config changes between calls", "[app_paths]") {
    EnvScope scope;
    unsetEnv("MAGDA_DATA_DIR");
    magda::Config::getInstance().setDataDir("/tmp/magda-rev1");
    paths::resolve();
    REQUIRE(paths::dataDir() == juce::File("/tmp/magda-rev1"));

    magda::Config::getInstance().setDataDir("/tmp/magda-rev2");
    paths::resolve();
    REQUIRE(paths::dataDir() == juce::File("/tmp/magda-rev2"));
}

TEST_CASE("paths::presetsDir / renderDir resolution chain works", "[app_paths]") {
    EnvScope scope;
    unsetEnv("MAGDA_PRESETS_DIR");
    unsetEnv("MAGDA_RENDER_DIR");
    magda::Config::getInstance().setPresetsDir("/tmp/magda-presets");
    magda::Config::getInstance().setRenderFolder("/tmp/magda-renders");
    paths::resolve();

    REQUIRE(paths::presetsDir() == juce::File("/tmp/magda-presets"));
    REQUIRE(paths::renderDir() == juce::File("/tmp/magda-renders"));

    setEnv("MAGDA_PRESETS_DIR", "/tmp/env-presets");
    setEnv("MAGDA_RENDER_DIR", "/tmp/env-renders");
    paths::resolve();

    REQUIRE(paths::presetsDir() == juce::File("/tmp/env-presets"));
    REQUIRE(paths::renderDir() == juce::File("/tmp/env-renders"));
    REQUIRE(paths::presetsDirOverriddenByEnv());
    REQUIRE(paths::renderDirOverriddenByEnv());
}
