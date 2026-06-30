cmake_minimum_required(VERSION 3.24)

# macOS: support back to Big Sur (first Apple Silicon release)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Windows has no system libxml2 (see vcpkg.json); it comes from vcpkg, which CI
# wires in via -DCMAKE_TOOLCHAIN_FILE. For local dev (CLion, plain cmake, the
# Makefile) auto-detect a vcpkg install from the VCPKG_ROOT environment variable
# so find_package(LibXml2) resolves without anyone passing the toolchain by hand.
# Must run before project(); an explicit -DCMAKE_TOOLCHAIN_FILE still wins, and
# on macOS/Linux (system libxml2, no VCPKG_ROOT) this is a no-op.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(_magda_vcpkg_toolchain "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    if(EXISTS "${_magda_vcpkg_toolchain}")
        set(CMAKE_TOOLCHAIN_FILE "${_magda_vcpkg_toolchain}"
            CACHE STRING "Vcpkg toolchain file")
    endif()
    unset(_magda_vcpkg_toolchain)
endif()

# Derive the version dynamically instead of hardcoding it. Prefer an explicit
# -DMAGDA_FULL_VERSION (set by release CI); otherwise read it from git tags
# (e.g. "0.12.0-rc2" or "0.12.0-rc2-15-g1391223"). This runs before project() so
# the numeric component can feed project(VERSION ...). MAGDA_FALLBACK_VERSION
# covers builds with no git history or tags (e.g. a source tarball).
set(MAGDA_FALLBACK_VERSION "0.1.0")
if(NOT DEFINED MAGDA_FULL_VERSION OR MAGDA_FULL_VERSION STREQUAL "")
    execute_process(
        COMMAND git describe --tags --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE MAGDA_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(GIT_DESCRIBE_RESULT EQUAL 0 AND MAGDA_GIT_VERSION MATCHES "^v?(.+)$")
        set(MAGDA_FULL_VERSION "${CMAKE_MATCH_1}")
    else()
        set(MAGDA_FULL_VERSION "${MAGDA_FALLBACK_VERSION}")
    endif()
endif()

# Numeric dotted version for project() and the executable metadata (macOS
# CFBundleShortVersionString / CFBundleVersion, Windows VERSIONINFO). These
# require a plain X.Y.Z, so strip any pre-release / git-describe suffix from
# MAGDA_FULL_VERSION (e.g. "0.12.0-rc2-15-g1391223" -> "0.12.0"). Falls back when
# there is no leading numeric component (e.g. a bare commit hash from --always).
if(MAGDA_FULL_VERSION MATCHES "^v?([0-9]+(\\.[0-9]+)+)")
    set(MAGDA_BUNDLE_VERSION "${CMAKE_MATCH_1}")
else()
    set(MAGDA_BUNDLE_VERSION "${MAGDA_FALLBACK_VERSION}")
endif()

project(Magda VERSION ${MAGDA_BUNDLE_VERSION} LANGUAGES C CXX)

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
    # Debug uses /Z7 (debug info embedded in each .obj) rather than /Zi (a
    # separate, shared compile-time PDB). /Z7 has no shared PDB, so:
    #   - parallel cl.exe workers never contend on a PDB (no /FS needed), and
    #   - the .obj is self-contained, which is what makes ccache able to cache
    #     MSVC Debug builds. /Zi can't be cached (the PDB write is a side effect
    #     ccache can't replay) - that's what broke the earlier sccache attempt.
    # Release still ships a real PDB for crash symbolication via per-target /Zi
    # in magda/daw/CMakeLists.txt, so /FS is scoped to Release only.
    set(CMAKE_CXX_FLAGS_DEBUG "/Z7 /Od /W3")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    # /FS serialises PDB writes across parallel cl.exe workers, required with the
    # Release per-target /Zi. Added via add_compile_options so it composes with
    # CMake's default MSVC init flags instead of replacing them.
    add_compile_options($<$<CONFIG:Release>:/FS>)
    # NOMINMAX: stop <windows.h> defining min()/max() macros, which clobber
    # std::min/std::max. The Faust poly instruments (poly-dsp.h) transitively
    # pull in <windows.h>, so without this they fail to compile on MSVC.
    add_compile_definitions(NOMINMAX)
    # /OPT:REF,ICF preserve release dead-stripping; /Zi + /DEBUG applied per-target (see magda/daw/CMakeLists.txt)
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /OPT:REF /OPT:ICF")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

# Options
option(MAGDA_BUILD_TESTS "Build tests" ON)
option(MAGDA_BUILD_EXAMPLES "Build developer example tools (e.g. lua_smoke REPL)" OFF)
option(MAGDA_BUILD_JUCE_ADAPTER "Build JUCE/Tracktion adapter" OFF)

# CLAP semantic-search backend (ONNX Runtime). Forced off on Intel macOS because
# upstream ORT dropped osx-x86_64 prebuilts in 1.26. The rest of the media DB
# (catalogue, indexing, metadata search) still works without it.
set(_magda_clap_default ON)
if(APPLE)
    # ORT 1.26 only ships single-arch binaries, so any build that targets
    # x86_64 (Intel-only or arm64+x86_64 universal) must disable CLAP.
    if(CMAKE_OSX_ARCHITECTURES)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64|i386")
            set(_magda_clap_default OFF)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i386")
        set(_magda_clap_default OFF)
    endif()
endif()
option(MAGDA_HAVE_CLAP "Enable CLAP audio/text encoders via ONNX Runtime" ${_magda_clap_default})

# Find packages
find_package(Threads REQUIRED)
find_package(LibXml2 REQUIRED)

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

# Hide host symbols by default so hosted plugins with their own static JUCE copy
# cannot bind to MAGDA's JUCE definitions at load time.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

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
    # Vendored llama.cpp leans on implicit numeric conversions (size_t->int,
    # double->float, etc.), so MSVC emits ~750 benign C4244/C4267/C4305
    # warnings from src/ and src/models/. Not our code, not actionable, and the
    # diagnostic volume slows the build; silence them on the llama target.
    # C4101 = unreferenced local variable (also upstream, also benign).
    if(TARGET llama)
        target_compile_options(llama PRIVATE /wd4244 /wd4267 /wd4305 /wd4101)
    endif()
endif()

# Faust — runtime DSP compilation. Free tier uses the interpreter backend (no
# LLVM, smaller binary, ~5–10× slower DSP execution); pro tier (later) flips
# MAGDA_FAUST_BACKEND=llvm to pull in the LLVM JIT backend at full speed.
set(MAGDA_FAUST_BACKEND "interp" CACHE STRING "Faust backend: interp or llvm")
set_property(CACHE MAGDA_FAUST_BACKEND PROPERTY STRINGS "interp" "llvm")

set(INCLUDE_EXECUTABLE ON CACHE BOOL "" FORCE)
set(INCLUDE_STATIC ON CACHE BOOL "" FORCE)
set(INCLUDE_DYNAMIC OFF CACHE BOOL "" FORCE)
set(INCLUDE_OSC OFF CACHE BOOL "" FORCE)
set(INCLUDE_HTTP OFF CACHE BOOL "" FORCE)
set(INCLUDE_ITP OFF CACHE BOOL "" FORCE)
set(INCLUDE_EMCC OFF CACHE BOOL "" FORCE)
set(INCLUDE_WASM_GLUE OFF CACHE BOOL "" FORCE)
set(INCLUDE_WASMTIME OFF CACHE BOOL "" FORCE)
# Default all backends OFF; flip the one we want below.
set(C_BACKEND       OFF CACHE STRING "" FORCE)
# CPP backend powers `faust -lang cpp …` for build-time DSP → native
# plugin compilation (see #1214). The `COMPILER` token enables the
# backend in the standalone `faust` executable; we don't need STATIC /
# DYNAMIC because libfaust isn't asked to emit C++ at runtime.
set(CPP_BACKEND     "COMPILER" CACHE STRING "" FORCE)
set(CMAJOR_BACKEND  OFF CACHE STRING "" FORCE)
set(CSHARP_BACKEND  OFF CACHE STRING "" FORCE)
set(DLANG_BACKEND   OFF CACHE STRING "" FORCE)
set(FIR_BACKEND     OFF CACHE STRING "" FORCE)
set(JAVA_BACKEND    OFF CACHE STRING "" FORCE)
set(JAX_BACKEND     OFF CACHE STRING "" FORCE)
set(JULIA_BACKEND   OFF CACHE STRING "" FORCE)
set(JSFX_BACKEND    OFF CACHE STRING "" FORCE)
set(OLDCPP_BACKEND  OFF CACHE STRING "" FORCE)
set(RUST_BACKEND    OFF CACHE STRING "" FORCE)
set(TEMPLATE_BACKEND OFF CACHE STRING "" FORCE)
set(VHDL_BACKEND    OFF CACHE STRING "" FORCE)
set(WASM_BACKEND    OFF CACHE STRING "" FORCE)
set(INTERP_BACKEND  OFF CACHE STRING "" FORCE)

if(MAGDA_FAUST_BACKEND STREQUAL "llvm")
    set(LLVM_BACKEND "STATIC" CACHE STRING "" FORCE)
    set(INTERP_COMP_BACKEND OFF CACHE STRING "" FORCE)
else()
    set(LLVM_BACKEND OFF CACHE STRING "" FORCE)
    # INTERP_COMP includes both interpreter VM + compile-from-source path —
    # what createInterpreterDSPFactoryFromString() needs.
    set(INTERP_COMP_BACKEND "STATIC" CACHE STRING "" FORCE)
endif()
add_subdirectory(third_party/faust/build third_party/faust EXCLUDE_FROM_ALL)
# Faust's static lib target is named `staticlib`. Re-expose under a clearer
# alias so call sites read sensibly.
add_library(faust::libfaust ALIAS staticlib)

# Faust's compiler/tlib/compatibility.cpp picks one of two implementations
# of `chdir` / `mkdir` / `getcwd` / `getFaustPathname` via:
#   #if defined(_MBCS) || __MINGW32__   // narrow:  char  buffers + CreateDirectoryA
#   #else                               // wide:    wchar buffers + CreateDirectory macro
#
# When the build defines neither `_MBCS` nor `UNICODE` (CMake's MSVC
# default with `CharacterSet=NotSet`) the file picks the wide branch,
# but the Win32 `CreateDirectory` macro resolves to `CreateDirectoryA`
# (the char* version) — a wchar_t* / char* mismatch and a C2664 build
# error. Define `_MBCS` for Faust's staticlib AND the `faust` executable
# (both compile compatibility.cpp) so they take the narrow branch
# upstream Faust expects on Windows; other targets are untouched.
# The Faust compiler library is vendored third-party code (~390 TUs) that
# leans on implicit numeric conversions and variable shadowing, flooding the
# build with benign warnings: MSVC C4244/C4267/C4305, GCC/Clang -Wshadow /
# -Wunused-* / -Wcast-function-type, etc. None are actionable in code we don't
# own, and the diagnostic volume slows the build, so silence warnings on both
# Faust targets. MSVC additionally needs _MBCS (see above).
if(MSVC)
    set(_faust_quiet /wd4244 /wd4267 /wd4305)
else()
    set(_faust_quiet -w)
endif()
foreach(_faust_target staticlib faust)
    if(TARGET ${_faust_target})
        target_compile_options(${_faust_target} PRIVATE ${_faust_quiet})
        if(MSVC)
            target_compile_definitions(${_faust_target} PRIVATE _MBCS)
        endif()
    endif()
endforeach()

# ----------------------------------------------------------------------------
# magda_compile_faust_dsp(<dsp_file> <class_name> <out_var>)
#
# Add a build-time step that runs `faust -lang cpp` on `<dsp_file>`,
# generating a self-contained C++ class named `<class_name>`. Stores the
# absolute path of the generated .cpp in `<out_var>` so the caller can
# add it to `target_sources(...)`.
#
# The output lives under `${CMAKE_BINARY_DIR}/compiled_dsps/`. Faust's
# `-cpp` backend is compiled into the `faust` executable via the
# `CPP_BACKEND=COMPILER` setting above.
# ----------------------------------------------------------------------------
function(magda_compile_faust_dsp DSP_FILE CLASS_NAME OUT_VAR)
    get_filename_component(DSP_NAME "${DSP_FILE}" NAME_WE)
    set(GENERATED_DIR "${CMAKE_BINARY_DIR}/compiled_dsps")
    set(GENERATED_CPP "${GENERATED_DIR}/${DSP_NAME}.generated.cpp")

    file(MAKE_DIRECTORY "${GENERATED_DIR}")

    add_custom_command(
        OUTPUT "${GENERATED_CPP}"
        COMMAND $<TARGET_FILE:faust>
                -lang cpp
                -cn ${CLASS_NAME}
                -I "${CMAKE_SOURCE_DIR}/third_party/faust/libraries"
                -single
                -o "${GENERATED_CPP}"
                "${DSP_FILE}"
        DEPENDS "${DSP_FILE}" faust
        COMMENT "faust → cpp: ${DSP_NAME}.dsp → ${CLASS_NAME}"
        VERBATIM
    )

    set(${OUT_VAR} "${GENERATED_CPP}" PARENT_SCOPE)
endfunction()

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

# Lua 5.4 — embedded scripting runtime (issue #29).
# Lua's official source distribution ships no CMakeLists.txt, so
# FetchContent_MakeAvailable just populates lua_SOURCE_DIR; the actual
# static library is built by magda/scripting/CMakeLists.txt.
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
)
FetchContent_MakeAvailable(lua)

# SQLite 3.53.1 — embedded media DB engine (issue #768).
# Like Lua, the official amalgamation ships no CMakeLists, so FetchContent
# just populates sqlite3_SOURCE_DIR and the static library is built by
# third_party/sqlite/CMakeLists.txt with FTS5 and foreign keys enabled.
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3530100.zip
    URL_HASH SHA3_256=3c07136e4f6b5dd0c395be86455014039597bc65b6851f7111e88f71b6e06114
)
FetchContent_MakeAvailable(sqlite3)
add_subdirectory(third_party/sqlite)

# ONNX Runtime 1.26.0 — CLAP inference engine for the media DB (issue #768).
# Microsoft ships prebuilt binaries per (OS, arch); we pin one per platform.
# Skipped on Intel macOS (MAGDA_HAVE_CLAP=OFF) since upstream dropped
# osx-x86_64 in 1.26 — those builds ship without semantic search.
if(MAGDA_HAVE_CLAP)
    set(ORT_VERSION 1.26.0)
    if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
                  OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
        set(ORT_URL  "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz")
        set(ORT_HASH SHA256=7a1280bbb1701ea514f71828765237e7896e0f2e1cd332f1f70dbd5c3e33aca3)
    elseif(UNIX AND NOT APPLE)
        set(ORT_URL  "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz")
        set(ORT_HASH SHA256=1254da24fb389cf39dc0ff3451ab48301740ffbfcbaf646849df92f80ee92c57)
    elseif(WIN32)
        set(ORT_URL  "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")
        set(ORT_HASH SHA256=6ebe99b5564bf4d029b6e93eac9ff423682b6212eade769e9ca3f685eaf500b4)
    else()
        message(FATAL_ERROR "ONNX Runtime: no prebuilt binary for this platform (set MAGDA_HAVE_CLAP=OFF to skip)")
    endif()
    FetchContent_Declare(
        onnxruntime
        URL      ${ORT_URL}
        URL_HASH ${ORT_HASH}
    )
    FetchContent_MakeAvailable(onnxruntime)
    add_subdirectory(third_party/onnxruntime)
endif()

# Simple agent system - no external dependencies needed

# Mutable Instruments DSP (Elements / Rings / Clouds) - native ports (#1581).
# Trimmed MIT fork at third_party/eurorack: stmlib + the three modules' DSP only.
# Built with TEST so stmlib substitutes its portable C paths for the Cortex-M4
# inline asm (saturating arithmetic, sqrt) and drops firmware section attrs;
# the results are numerically equivalent. TEST is PUBLIC so any consumer that
# includes these headers gets the same portable path (magda-core uses no bare
# TEST macro of its own, so there is no collision).
set(MI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eurorack)
file(GLOB MI_SOURCES CONFIGURE_DEPENDS
    ${MI_ROOT}/elements/dsp/*.cc
    ${MI_ROOT}/elements/resources.cc
    ${MI_ROOT}/rings/dsp/*.cc
    ${MI_ROOT}/rings/resources.cc
    ${MI_ROOT}/clouds/dsp/*.cc
    ${MI_ROOT}/clouds/dsp/pvoc/*.cc
    ${MI_ROOT}/clouds/resources.cc
    ${MI_ROOT}/stmlib/utils/random.cc
    ${MI_ROOT}/stmlib/dsp/atan.cc
    ${MI_ROOT}/stmlib/dsp/units.cc
)
add_library(magda_mutable STATIC ${MI_SOURCES})
target_include_directories(magda_mutable PUBLIC ${MI_ROOT})
target_compile_definitions(magda_mutable PUBLIC TEST)
if(MSVC)
    target_compile_definitions(magda_mutable PUBLIC _USE_MATH_DEFINES)

    # This is vendored Mutable Instruments DSP. It leans on implicit numeric
    # conversions throughout (double literals -> float, size_t -> int32, etc.),
    # so MSVC emits a flood of benign conversion warnings across stmlib headers
    # and every dsp/*.cc - tens of thousands of them, whose formatting + log
    # I/O measurably slow the build. We don't own this code and these are not
    # actionable, so silence them for the whole target:
    #   C4305 - truncation double -> float (the literal tables, the worst spam)
    #   C4244 - conversion, possible loss of data
    #   C4267 - size_t -> smaller int conversion
    target_compile_options(magda_mutable PRIVATE /wd4305 /wd4244 /wd4267)

    # The generated resources.cc files are giant brace-initialised const float
    # lookup tables (elements/resources.cc alone is ~44k lines of FP literals).
    # MSVC's optimiser is pathologically slow constant-folding huge static
    # initialiser lists, so /O2 burns minutes on data it can't optimise. Disable
    # optimisation for just these TUs: pure tables, zero runtime cost.
    # TARGET_DIRECTORY scopes the per-file flag to magda_mutable (supported since
    # CMake 3.18; verified the flag actually lands).
    set_source_files_properties(
        ${MI_ROOT}/elements/resources.cc
        ${MI_ROOT}/rings/resources.cc
        ${MI_ROOT}/clouds/resources.cc
        TARGET_DIRECTORY magda_mutable
        PROPERTIES COMPILE_OPTIONS "/Od")
else()
    # Same vendored-DSP rationale on GCC/Clang: shadowing + unused-var warnings
    # from stmlib headers and dsp/*.cc that we won't fix. Silence them.
    target_compile_options(magda_mutable PRIVATE -w)
endif()
set_target_properties(magda_mutable PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(magda::mutable ALIAS magda_mutable)

# Product-based subdirectories
add_subdirectory(magda/daw)
add_subdirectory(magda/scripting)
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
if(NOT APPLE)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lang"
        DESTINATION bin
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/controllers"
        DESTINATION bin
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/models"
        DESTINATION bin
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dawproject"
        DESTINATION bin
    )
endif()
