#pragma once

#include <juce_core/juce_core.h>

// Centralised filesystem path resolution for MAGDA's per-user files.
//
// Three configurable roots (resolved in this order — first non-empty wins):
//   1. Environment variable (MAGDA_DATA_DIR / MAGDA_PRESETS_DIR / MAGDA_RENDER_DIR)
//   2. magda::Config (Preferences → Paths)
//   3. OS-default (juce::File::getSpecialLocation)
//
// Resolution happens at startup via resolve(); for the hot-swappable knobs
// (presets, render) it can be re-run after Config changes. Once resolved,
// every path consumer goes through one of the named accessors below — no
// caller should bake juce::File::getSpecialLocation(...) into its own logic.
//
// The CONFIG FILE itself stays at alwaysOSDefault() / "config.json", because
// Config has to be loaded BEFORE the configured data dir is known.

namespace magda::paths {

// ---------------------------------------------------------------------------
// Resolved roots
// ---------------------------------------------------------------------------

/** Per-user data: logs, config, scripts, plugin caches, controller profiles. */
juce::File dataDir();

/** Per-user user-facing presets (Chains, Racks, Devices). */
juce::File presetsDir();

/** Default destination for rendered audio. */
juce::File renderDir();

/** OS-default appdata root (`~/Library/Application Support/MAGDA` on macOS).
 *  Returned regardless of override — needed because the config file itself
 *  is anchored here so the override can be read from it. */
juce::File alwaysOSDefault();

// ---------------------------------------------------------------------------
// Computed subpaths under dataDir()
// ---------------------------------------------------------------------------

juce::File logsDir();                // dataDir() / "Logs"
juce::File controllerScriptsDir();   // dataDir() / "Scripts" / "Controllers"
juce::File controllerProfilesDir();  // dataDir() / "controllers"
juce::File pluginConfigsDir();       // dataDir() / "PluginConfigs"
juce::File drumkitsDir();            // presetsDir() / "Drumkits"

juce::File configFile();                                      // alwaysOSDefault() / "config.json"
juce::File pluginListFile();                                  // dataDir() / "PluginList.xml"
juce::File pluginCacheFile();                                 // dataDir() / "PluginCache.json"
juce::File pluginExclusionsFile();                            // dataDir() / "plugin_exclusions.txt"
juce::File pluginScanMarkerFile(const juce::String& format);  // dataDir() / "scanning_<format>.txt"
juce::File lastScanReportFile();                              // dataDir() / "last_scan_report.txt"
juce::File pluginFavoritesFile();                             // dataDir() / "plugin_favorites.xml"
juce::File pluginAliasesFile();                               // dataDir() / "plugin_aliases.xml"
juce::File pluginPreferencesFile();  // dataDir() / "plugin_preferences.json"
juce::File parameterDetectorLog();   // dataDir() / "param_detector.log"

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

/** Read env vars + (if loaded) Config and update the cached resolved roots.
 *
 *  Safe to call from anywhere; cheap. Called twice during startup:
 *    1. Before the file logger is created (Config not yet loaded — env vars
 *       and OS defaults take effect).
 *    2. After Config::load() — picks up persisted user overrides.
 *
 *  Also safe to call after Config::set{Data,Presets,Render}Dir at runtime to
 *  apply the change to subsequent path queries (the data-dir knob still
 *  requires a restart for full effect because file handles are already open;
 *  that's a UX choice, not a technical limit of resolve()).
 */
void resolve();

/** True iff MAGDA_DATA_DIR / MAGDA_PRESETS_DIR / MAGDA_RENDER_DIR is currently
 *  in effect for the corresponding root. Used by the Paths preferences page
 *  to mark a knob as "overridden by env var — Config setting is ignored". */
bool dataDirOverriddenByEnv();
bool presetsDirOverriddenByEnv();
bool renderDirOverriddenByEnv();

}  // namespace magda::paths
