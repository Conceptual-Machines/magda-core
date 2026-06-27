#include "AppPaths.hpp"

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>

#include "Config.hpp"

namespace magda::paths {

namespace {

// Resolved roots are stored as lazy holders behind a mutex so resolve() can
// update them safely from the message thread while readers (potentially on
// other threads — e.g. plugin scanner subprocess controller) take a stable
// snapshot. Reads happen vastly more often than resolve() runs, so the
// shared_ptr swap is cheaper than copying juce::String into atomics.
struct Resolved {
    juce::File data;
    juce::File presets;
    juce::File render;
    bool dataFromEnv = false;
    bool presetsFromEnv = false;
    bool renderFromEnv = false;
};

std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}

std::shared_ptr<const Resolved>& cachedSlot() {
    static std::shared_ptr<const Resolved> ptr;
    return ptr;
}

std::shared_ptr<const Resolved> snapshot() {
    std::lock_guard<std::mutex> lock(cacheMutex());
    if (cachedSlot() == nullptr) {
        // Bootstrap: first reader before resolve() ran. Compute defaults
        // on demand so paths remain useful even if a consumer touches them
        // before MagdaDAWApplication::initialise() reaches its resolve()
        // call (e.g. unit tests that don't run a JUCE app).
        auto fresh = std::make_shared<Resolved>();
        fresh->data = alwaysOSDefault();
        fresh->presets = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                             .getChildFile("MAGDA")
                             .getChildFile("Presets");
        fresh->render = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("MAGDA")
                            .getChildFile("Renders");
        cachedSlot() = fresh;
    }
    return cachedSlot();
}

// JUCE doesn't expose getEnvironmentVariable on every platform via the
// SystemStats subset we link; use std::getenv directly.
juce::String envVar(const char* name) {
    if (const char* v = std::getenv(name); v != nullptr && v[0] != '\0')
        return juce::String::fromUTF8(v);
    return {};
}

juce::File defaultPresets() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("MAGDA")
        .getChildFile("Presets");
}

juce::File defaultRender() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("MAGDA")
        .getChildFile("Renders");
}

// Combine override with default. Returns {file, fromEnv}: env wins over
// configured, configured wins over default. fromEnv is true only when the
// env var was used.
struct ResolveOne {
    juce::File file;
    bool fromEnv;
};

ResolveOne resolveOne(const juce::String& envVal, const std::string& configVal,
                      const juce::File& fallback) {
    if (envVal.isNotEmpty())
        return {juce::File(envVal), true};
    if (!configVal.empty())
        return {juce::File(
                    juce::String::fromUTF8(configVal.c_str(), static_cast<int>(configVal.size()))),
                false};
    return {fallback, false};
}

}  // namespace

// ---------------------------------------------------------------------------
// Resolved roots
// ---------------------------------------------------------------------------

juce::File dataDir() {
    return snapshot()->data;
}

juce::File presetsDir() {
    return snapshot()->presets;
}

juce::File renderDir() {
    return snapshot()->render;
}

juce::File alwaysOSDefault() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA");
}

// ---------------------------------------------------------------------------
// Computed subpaths
// ---------------------------------------------------------------------------

juce::File logsDir() {
    return dataDir().getChildFile("Logs");
}

juce::File controllerScriptsDir() {
    return dataDir().getChildFile("Scripts").getChildFile("Controllers");
}

juce::File controllerProfilesDir() {
    return dataDir().getChildFile("controllers");
}

juce::File pluginConfigsDir() {
    return dataDir().getChildFile("PluginConfigs");
}

juce::File drumkitsDir() {
    return presetsDir().getChildFile("Drumkits");
}

juce::File configFile() {
    return alwaysOSDefault().getChildFile("config.json");
}

juce::File pluginListFile() {
    return dataDir().getChildFile("PluginList.xml");
}

juce::File pluginCacheFile() {
    return dataDir().getChildFile("PluginCache.json");
}

juce::File pluginExclusionsFile() {
    return dataDir().getChildFile("plugin_exclusions.txt");
}

juce::File pluginScanMarkerFile(const juce::String& format) {
    return dataDir().getChildFile("scanning_" + format + ".txt");
}

juce::File lastScanReportFile() {
    return dataDir().getChildFile("last_scan_report.txt");
}

juce::File pluginFavoritesFile() {
    return dataDir().getChildFile("plugin_favorites.xml");
}

juce::File pluginAliasesFile() {
    return dataDir().getChildFile("plugin_aliases.xml");
}

juce::File pluginPreferencesFile() {
    return dataDir().getChildFile("plugin_preferences.json");
}

juce::File pluginCapabilitiesFile() {
    return dataDir().getChildFile("plugin_capabilities.json");
}

juce::File parameterDetectorLog() {
    return dataDir().getChildFile("param_detector.log");
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

void resolve() {
    auto fresh = std::make_shared<Resolved>();

    // Config may not be loaded yet on the first call; getInstance() returns
    // a default-initialised instance whose path getters return empty strings,
    // which the resolveOne fallback handles correctly.
    auto& cfg = Config::getInstance();

    auto data = resolveOne(envVar("MAGDA_DATA_DIR"), cfg.getDataDir(), alwaysOSDefault());
    auto presets = resolveOne(envVar("MAGDA_PRESETS_DIR"), cfg.getPresetsDir(), defaultPresets());
    auto render = resolveOne(envVar("MAGDA_RENDER_DIR"), cfg.getRenderFolder(), defaultRender());

    fresh->data = data.file;
    fresh->presets = presets.file;
    fresh->render = render.file;
    fresh->dataFromEnv = data.fromEnv;
    fresh->presetsFromEnv = presets.fromEnv;
    fresh->renderFromEnv = render.fromEnv;

    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        cachedSlot() = fresh;
    }
}

bool dataDirOverriddenByEnv() {
    return snapshot()->dataFromEnv;
}

bool presetsDirOverriddenByEnv() {
    return snapshot()->presetsFromEnv;
}

bool renderDirOverriddenByEnv() {
    return snapshot()->renderFromEnv;
}

}  // namespace magda::paths
