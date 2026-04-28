#pragma once

extern "C" {
struct lua_State;
}

namespace magda::scripting {

/**
 * Strip globals that would let untrusted Lua code escape the host process —
 * arbitrary file I/O, shell execution, dynamic library loading, raw bytecode
 * loading. Call after luaL_openlibs(); must be applied to every fresh state
 * before user code runs.
 *
 * What is removed:
 *   - dofile, loadfile, load, loadstring  (no chunk loading from Lua side;
 *                                          host loads via lua_load directly)
 *   - io                                  (full stdio table dropped)
 *   - package, require                    (no C library loading or module fs lookup)
 *   - debug                               (introspection / stack rewriting)
 *   - os.execute, os.remove, os.rename,
 *     os.exit, os.tmpname, os.getenv,
 *     os.setlocale                        (process / filesystem effects)
 *
 * What stays usable:
 *   - string, math, table, utf8, coroutine
 *   - print  (redirected to juce::Logger by LuaRuntime)
 *   - os.date, os.time, os.difftime, os.clock  (read-only timing)
 */
void applySandbox(lua_State* L);

}  // namespace magda::scripting
