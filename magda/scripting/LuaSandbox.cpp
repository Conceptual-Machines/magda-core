#include "magda/scripting/LuaSandbox.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <string>

namespace magda::scripting {

namespace {

// C closure: raises a descriptive error using the name passed as upvalue 1.
// Used both as a stand-in for blocked function globals (dofile, os.execute)
// and as the __index/__newindex/__call handler on blocked global tables (io,
// package, debug) — so any access to a blocked thing produces a clear
// message instead of "attempt to call/index a nil value".
int sandboxBlocker(lua_State* L) {
    const char* name = lua_tostring(L, lua_upvalueindex(1));
    return luaL_error(L, "'%s' is disabled in the MAGDA Lua sandbox",
                      name != nullptr ? name : "(unknown)");
}

// _G[name] = function() error("...") end
void blockGlobalCall(lua_State* L, const char* name) {
    lua_pushstring(L, name);
    lua_pushcclosure(L, sandboxBlocker, 1);
    lua_setglobal(L, name);
}

// os[field] = function() error("...") end  (with name "os.field")
void blockOsField(lua_State* L, const char* field) {
    lua_getglobal(L, "os");
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1);
        return;
    }
    std::string full = std::string("os.") + field;
    lua_pushstring(L, full.c_str());
    lua_pushcclosure(L, sandboxBlocker, 1);
    lua_setfield(L, -2, field);
    lua_pop(L, 1);
}

// _G[name] = setmetatable({}, { __index = blocker, __newindex = blocker,
//                                __call = blocker, __metatable = false })
// Any read, write, or call on the table errors with a sandbox message.
// The metatable is locked so user code can't strip it via setmetatable.
void blockGlobalTable(lua_State* L, const char* name) {
    lua_newtable(L);  // the empty stub table
    lua_newtable(L);  // its metatable

    // Stack on entry: [stub_table, metatable]. After pushstring + pushcclosure
    // we have [stub_table, metatable, closure] so the metatable is at -2.
    for (const char* meta : {"__index", "__newindex", "__call"}) {
        lua_pushstring(L, name);
        lua_pushcclosure(L, sandboxBlocker, 1);
        lua_setfield(L, -2, meta);
    }

    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");

    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
}

}  // namespace

void applySandbox(lua_State* L) {
    // Chunk loaders — the host loads scripts via the C API directly; user
    // code has no business compiling more code at runtime.
    blockGlobalCall(L, "dofile");
    blockGlobalCall(L, "loadfile");
    blockGlobalCall(L, "load");
    blockGlobalCall(L, "loadstring");
    blockGlobalCall(L, "require");

    // Whole-table removals.
    blockGlobalTable(L, "io");
    blockGlobalTable(L, "package");
    blockGlobalTable(L, "debug");

    // os: keep only read-only timing helpers; block process / filesystem
    // effects.
    blockOsField(L, "execute");
    blockOsField(L, "remove");
    blockOsField(L, "rename");
    blockOsField(L, "exit");
    blockOsField(L, "tmpname");
    blockOsField(L, "getenv");
    blockOsField(L, "setlocale");
}

}  // namespace magda::scripting
