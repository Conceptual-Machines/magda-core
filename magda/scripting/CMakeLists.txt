# Scripting - Embedded Lua runtime for MAGDA
#
# This module owns the embedded Lua VM (issue #29) and the MagdaApi
# bindings (#30) plus the MIDI controller bridge (#592). Because the
# bindings expose DAW types directly, magda_scripting links magda_daw —
# splitting the API types into a thin lib so this can be DAW-independent
# is tracked as future cleanup.

# ---------------------------------------------------------------------------
# Lua 5.4 static library — fetched in the root CMakeLists.txt.
# Lua's official source distribution does not ship a CMake build, so we
# compile the .c files directly.
# ---------------------------------------------------------------------------

set(LUA_SRC_DIR "${lua_SOURCE_DIR}/src")

set(LUA_CORE_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lutf8lib.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

add_library(lua_static STATIC ${LUA_CORE_SOURCES})

target_include_directories(lua_static
    PUBLIC
    ${LUA_SRC_DIR}
)

# Lua compiles as C; tell it which platform features are available so the
# stdlib (loadlib, os.execute, etc.) link properly. We will sandbox these at
# runtime, but the symbols still need to resolve.
if(APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
endif()

# Suppress warnings from Lua's source; this is third-party code and the
# project's -Wall -Wextra -Wpedantic flags trigger many false positives in it.
if(MSVC)
    target_compile_options(lua_static PRIVATE /w)
else()
    target_compile_options(lua_static PRIVATE -w)
endif()

set_target_properties(lua_static PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# magda_scripting — thin C++ wrapper exposing LuaRuntime + LuaSandbox.
# ---------------------------------------------------------------------------

set(MAGDA_SCRIPTING_SOURCES
    LuaRuntime.hpp
    LuaRuntime.cpp
    LuaSandbox.hpp
    LuaSandbox.cpp
    MagdaApiLuaBindings.hpp
    MagdaApiLuaBindings.cpp
    LuaController.hpp
    LuaController.cpp
    LuaScriptStore.hpp
    LuaScriptStore.cpp
)

add_library(magda_scripting STATIC ${MAGDA_SCRIPTING_SOURCES})

target_link_libraries(magda_scripting
    PUBLIC
    lua_static
    # juce_core comes transitively from magda_daw (compiled once there); a
    # direct link would recompile the JUCE stack into this target again.
    # MagdaApi headers live under magda/daw/api/. The bindings need the
    # full DAW types (TrackInfo, ClipInfo, ProjectInfo). Long-term these
    # types could be split into a thin "magda_api_types" lib so scripting
    # doesn't pull all of magda_daw, but that refactor is out of scope
    # for #30.
    magda_daw
)

target_include_directories(magda_scripting
    PUBLIC
    ${CMAKE_SOURCE_DIR}
)

set_target_properties(magda_scripting PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(magda_scripting PRIVATE /W4)
else()
    target_compile_options(magda_scripting PRIVATE -Wall -Wextra -Wpedantic)
endif()
