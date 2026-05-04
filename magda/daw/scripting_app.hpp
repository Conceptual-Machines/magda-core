#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// App-level Lua-controller-script wiring (issue #592).
//
// LuaController itself lives in magda_scripting. MagdaDAWApplication (in
// magda_daw_app) owns it and registers it with the engine's MidiBridge.
// These free functions are the way the rest of the app — primarily the
// Lua Scripts section in ControllersDialog — reaches it without needing
// visibility into the JUCEApplication subclass or pulling magda_scripting
// into magda_daw.
//
// All implementations live in magda_daw_main.cpp.

namespace magda::scripting_app {

struct LuaScriptPorts {
    juce::String midiOutputPort;
    juce::String dawInputPort;
};

/** Reload the active Lua controller script. If no script is currently active,
 *  picks the alphabetically-first script in the scripts folder. Returns true
 *  on success. */
bool reloadActiveLuaScript();

/** Load `file` as the active Lua controller script, replacing whatever is
 *  loaded now. Returns true on success. */
bool loadLuaScript(const juce::File& file);

/** Drop the active script. Subsequent MIDI events become no-ops until a
 *  script is loaded again. */
void unloadLuaScript();

/** Filename of the currently active Lua script, or empty if none. */
juce::String activeLuaScriptName();

/** Persisted MIDI assignment for a script filename. */
LuaScriptPorts luaScriptPorts(const juce::String& scriptName);

/** Update the MIDI assignment for a script filename and persist it. */
void setLuaScriptPorts(const juce::String& scriptName, const LuaScriptPorts& ports);

/** True if there is at least one script available to load (any enabled
 *  factory script, or any user-imported script). Disambiguates "no scripts
 *  present" from "load failed" after a Reload — LuaController::loadScript
 *  clears the active name on any failure, so the dialog can't tell those
 *  two cases apart from activeLuaScriptName alone. */
bool hasAnyLuaScripts();

/** Lists user-imported scripts plus factory scripts the user has explicitly
 *  enabled (Config::enabledFactoryLuaScripts), sorted alphabetically. */
std::vector<juce::File> enumerateLuaScripts();

/** Lists factory scripts NOT yet enabled — what the "Add Script" picker
 *  should offer. Sorted alphabetically. */
std::vector<juce::File> enumerateAvailableFactoryLuaScripts();

/** True iff `file` lives in the bundled (factory) scripts directory. Used
 *  by the dialog to decide whether a row's "remove" should drop the entry
 *  from the enabled-factory list versus deleting the user-dir file. */
bool isFactoryLuaScript(const juce::File& file);

/** Per-user controller scripts folder. Created on demand. */
juce::File luaScriptsFolder();

/** Opens the per-user controller scripts folder in the OS file explorer.
 *  Creates the folder if it doesn't exist. */
void revealLuaScriptsFolder();

}  // namespace magda::scripting_app
