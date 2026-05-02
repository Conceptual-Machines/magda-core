#include "magda/scripting/LuaRuntime.hpp"

#include "magda/scripting/LuaSandbox.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstddef>
#include <utility>

namespace magda::scripting {

namespace {

// Replacement for Lua's default print(): forwards to juce::Logger.
// Mirrors the formatting of the stock implementation in lbaselib.c — coerces
// each argument with tostring(), separates with tabs, and emits a single line.
int luaPrintToLogger(lua_State* L) {
    int n = lua_gettop(L);
    juce::String line;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);  // pushes converted string
        if (i > 1)
            line << '\t';
        line << juce::String::fromUTF8(s, static_cast<int>(len));
        lua_pop(L, 1);  // pop converted string
    }
    juce::Logger::writeToLog(line);
    return 0;
}

// Message handler installed for lua_pcall — appends a traceback so error
// messages report the exact line in user code instead of just the bottom of
// the call stack.
int errorMessageHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (msg == nullptr) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

bool runChunk(lua_State* L, juce::String& errorOut) {
    // Stack: [ ..., chunk ]
    int base = lua_gettop(L);  // index of the chunk function
    lua_pushcfunction(L, errorMessageHandler);
    lua_insert(L, base);                // move handler below the chunk
    int rc = lua_pcall(L, 0, 0, base);  // 0 args, 0 results, handler at `base`
    lua_remove(L, base);                // remove handler
    if (rc != LUA_OK) {
        size_t len = 0;
        const char* msg = lua_tolstring(L, -1, &len);
        errorOut = msg ? juce::String::fromUTF8(msg, static_cast<int>(len))
                       : juce::String("(unknown Lua error)");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

}  // namespace

LuaRuntime::LuaRuntime() : L_(luaL_newstate()) {
    if (L_ == nullptr) {
        // Out of memory at startup — extremely unlikely. Leave L_ null;
        // every subsequent call short-circuits via the null check.
        lastError_ = "luaL_newstate returned null";
        return;
    }

    luaL_openlibs(L_);
    applySandbox(L_);

    // Replace print() with our logger-forwarding version.
    lua_pushcfunction(L_, luaPrintToLogger);
    lua_setglobal(L_, "print");
}

LuaRuntime::~LuaRuntime() {
    if (L_ != nullptr)
        lua_close(L_);
}

LuaRuntime::LuaRuntime(LuaRuntime&& other) noexcept
    : L_(std::exchange(other.L_, nullptr)), lastError_(std::move(other.lastError_)) {}

LuaRuntime& LuaRuntime::operator=(LuaRuntime&& other) noexcept {
    if (this != &other) {
        if (L_ != nullptr)
            lua_close(L_);
        L_ = std::exchange(other.L_, nullptr);
        lastError_ = std::move(other.lastError_);
    }
    return *this;
}

bool LuaRuntime::eval(const juce::String& chunk, const juce::String& chunkName) {
    if (L_ == nullptr)
        return false;

    lastError_ = {};
    auto chunkUtf8 = chunk.toRawUTF8();
    auto nameUtf8 = chunkName.toRawUTF8();
    auto chunkLen = static_cast<std::size_t>(chunk.getNumBytesAsUTF8());

    int rc = luaL_loadbuffer(L_, chunkUtf8, chunkLen, nameUtf8);
    if (rc != LUA_OK) {
        size_t len = 0;
        const char* msg = lua_tolstring(L_, -1, &len);
        lastError_ = msg ? juce::String::fromUTF8(msg, static_cast<int>(len))
                         : juce::String("(unknown Lua load error)");
        lua_pop(L_, 1);
        return false;
    }

    return runChunk(L_, lastError_);
}

bool LuaRuntime::evalFile(const juce::File& file) {
    if (!file.existsAsFile()) {
        lastError_ = "File does not exist: " + file.getFullPathName();
        return false;
    }
    juce::String contents = file.loadFileAsString();
    juce::String chunkName = "@" + file.getFullPathName();
    return eval(contents, chunkName);
}

std::optional<long long> LuaRuntime::evalToInt(const juce::String& chunk) {
    if (L_ == nullptr)
        return std::nullopt;

    // Wrap as `return (chunk)` so the value lands on the stack.
    juce::String wrapped = "return (" + chunk + ")";
    lastError_ = {};

    auto src = wrapped.toRawUTF8();
    auto srcLen = static_cast<std::size_t>(wrapped.getNumBytesAsUTF8());
    if (luaL_loadbuffer(L_, src, srcLen, "=eval") != LUA_OK) {
        size_t len = 0;
        const char* msg = lua_tolstring(L_, -1, &len);
        lastError_ = msg ? juce::String::fromUTF8(msg, static_cast<int>(len))
                         : juce::String("(unknown Lua load error)");
        lua_pop(L_, 1);
        return std::nullopt;
    }

    int base = lua_gettop(L_);
    lua_pushcfunction(L_, errorMessageHandler);
    lua_insert(L_, base);
    int rc = lua_pcall(L_, 0, 1, base);
    lua_remove(L_, base);
    if (rc != LUA_OK) {
        size_t len = 0;
        const char* msg = lua_tolstring(L_, -1, &len);
        lastError_ = msg ? juce::String::fromUTF8(msg, static_cast<int>(len))
                         : juce::String("(unknown Lua runtime error)");
        lua_pop(L_, 1);
        return std::nullopt;
    }

    if (!lua_isinteger(L_, -1)) {
        lua_pop(L_, 1);
        lastError_ = "result is not an integer";
        return std::nullopt;
    }
    long long result = static_cast<long long>(lua_tointeger(L_, -1));
    lua_pop(L_, 1);
    return result;
}

std::optional<juce::String> LuaRuntime::evalToString(const juce::String& chunk) {
    if (L_ == nullptr)
        return std::nullopt;

    juce::String wrapped = "return (" + chunk + ")";
    lastError_ = {};

    auto src = wrapped.toRawUTF8();
    auto srcLen = static_cast<std::size_t>(wrapped.getNumBytesAsUTF8());
    if (luaL_loadbuffer(L_, src, srcLen, "=eval") != LUA_OK) {
        size_t len = 0;
        const char* msg = lua_tolstring(L_, -1, &len);
        lastError_ = msg ? juce::String::fromUTF8(msg, static_cast<int>(len))
                         : juce::String("(unknown Lua load error)");
        lua_pop(L_, 1);
        return std::nullopt;
    }

    int base = lua_gettop(L_);
    lua_pushcfunction(L_, errorMessageHandler);
    lua_insert(L_, base);
    int rc = lua_pcall(L_, 0, 1, base);
    lua_remove(L_, base);
    if (rc != LUA_OK) {
        size_t len = 0;
        const char* msg = lua_tolstring(L_, -1, &len);
        lastError_ = msg ? juce::String::fromUTF8(msg, static_cast<int>(len))
                         : juce::String("(unknown Lua runtime error)");
        lua_pop(L_, 1);
        return std::nullopt;
    }

    if (lua_type(L_, -1) != LUA_TSTRING) {
        lua_pop(L_, 1);
        lastError_ = "result is not a string";
        return std::nullopt;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L_, -1, &len);
    juce::String result = juce::String::fromUTF8(s, static_cast<int>(len));
    lua_pop(L_, 1);
    return result;
}

}  // namespace magda::scripting
