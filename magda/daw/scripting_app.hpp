#pragma once

#include <juce_core/juce_core.h>

// App-level Lua-controller-script wiring (issue #592).
//
// LuaController itself lives in magda_scripting. MagdaDAWApplication (in
// magda_daw_app) owns it and registers it with the engine's MidiBridge.
// These free functions are the way the rest of the app — primarily the
// Reload button in ControllersDialog — reaches it without needing visibility
// into the JUCEApplication subclass or pulling magda_scripting into magda_daw.
//
// All implementations live in magda_daw_main.cpp.

namespace magda::scripting_app {

/** Reload the active Lua controller script from the per-user scripts
 *  folder. Returns true on success. */
bool reloadActiveLuaScript();

/** Filename of the currently active Lua script, or empty if none. */
juce::String activeLuaScriptName();

/** Opens the per-user controller scripts folder in the OS file explorer.
 *  Creates the folder if it doesn't exist. */
void revealLuaScriptsFolder();

}  // namespace magda::scripting_app
