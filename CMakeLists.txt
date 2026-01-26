cmake_minimum_required(VERSION 3.24)

project(Magda VERSION 0.1.0 LANGUAGES C CXX)

# ccache disabled - causes unnecessary rebuilds with Ninja
# The interaction between ccache, Ninja, and JUCE's glob verification
# causes all 258+ targets to rebuild on every invocation.
# TODO: Investigate proper ccache configuration for JUCE projects
# find_program(CCACHE_PROGRAM ccache)
# if(CCACHE_PROGRAM)
#     set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
#     set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
#     message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
# endif()

# Set C++ standard (required for Tracktion Engine)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE options
option(JUCE_BUILD_EXTRAS "Build JUCE Examples" OFF)
option(JUCE_BUILD_EXAMPLES "Build JUCE Extras" OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Options
option(MAGDA_BUILD_TESTS "Build tests" ON)
option(MAGDA_BUILD_EXAMPLES "Build examples" ON)
option(MAGDA_BUILD_JUCE_ADAPTER "Build JUCE/Tracktion adapter" OFF)

# Find packages
find_package(Threads REQUIRED)

# Include FetchContent for dependency management
include(FetchContent)

# Set policies for compatibility
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Set policy for minimum version compatibility
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Allow lower cmake version for dependencies
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# JSON handling - using JUCE's built-in JSON support instead of nlohmann/json
# FetchContent_Declare(
#     nlohmann_json
#     GIT_REPOSITORY https://github.com/nlohmann/json.git
#     GIT_TAG v3.11.3
# )

# gRPC removed - using simple in-process agent system instead

# Tracktion Engine for DAW functionality (using local submodule)
add_subdirectory(third_party/tracktion_engine)

# Catch2 for testing
if(MAGDA_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
endif()

# No external dependencies needed for simple agent system

if(MAGDA_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
endif()

# Simple agent system - no external dependencies needed

# Product-based subdirectories
add_subdirectory(magda/daw)
add_subdirectory(magda/agents)

# Examples
if(MAGDA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(MAGDA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# JUCE adapter (optional)
if(MAGDA_BUILD_JUCE_ADAPTER)
    add_subdirectory(adapters/juce)
endif()

# Install rules
install(TARGETS magda_daw_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
