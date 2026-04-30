# Examples - small standalone binaries that exercise individual modules.

# lua_smoke — interactive REPL / script runner for issue #29 verification.
# Useful for poking at the sandbox and print()-redirect before bindings (#30)
# and controller scripting (#592) land.
add_executable(lua_smoke lua_smoke.cpp)

target_link_libraries(lua_smoke
    PRIVATE
    magda_scripting
    juce::juce_core
)

target_include_directories(lua_smoke
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

set_target_properties(lua_smoke PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
