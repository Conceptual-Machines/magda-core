cmake_minimum_required(VERSION 3.24)

# macOS: support back to Big Sur (first Apple Silicon release)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(Magda VERSION 0.1.0 LANGUAGES C CXX)

# Derive display version from git tags (e.g. "0.1.0-rc2" or "0.1.0-rc2-15-g1391223")
execute_process(
    COMMAND git describe --tags --always
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE MAGDA_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_DESCRIBE_RESULT
)
if(GIT_DESCRIBE_RESULT EQUAL 0 AND MAGDA_GIT_VERSION MATCHES "^v?(.+)$")
    set(MAGDA_FULL_VERSION "${CMAKE_MATCH_1}")
else()
    set(MAGDA_FULL_VERSION "${PROJECT_VERSION}")
endif()

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
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /W3")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    # /OPT:REF,ICF preserve release dead-stripping; /Zi + /DEBUG applied per-target (see magda/daw/CMakeLists.txt)
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /OPT:REF /OPT:ICF")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

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

# LLM API client module (juce_core only)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/third_party/juce-llm/juce_llm)

# llama.cpp — embedded local inference
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
else()
    set(GGML_METAL OFF CACHE BOOL "" FORCE)
    # Disable -march=native so the binary runs on CPUs without AVX2/FMA/etc.
    # Without this, AppImages built on modern CI runners crash with SIGILL
    # on older CPUs (e.g. pre-Haswell).  Baseline: SSE4.2 + AVX.
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_AVX2   OFF CACHE BOOL "" FORCE)
    set(GGML_FMA    OFF CACHE BOOL "" FORCE)
    set(GGML_F16C   OFF CACHE BOOL "" FORCE)
    set(GGML_BMI2   OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(third_party/llama.cpp)

# llama.cpp doesn't need C++20; building as C++17 avoids MSVC char8_t errors
if(MSVC)
    set_target_properties(llama PROPERTIES CXX_STANDARD 17)
endif()

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
