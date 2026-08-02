# Build dependencies owned by the device layer. This module is included by the
# repository root before magda_devices is declared.

# Faust — runtime DSP compilation. The default is the WebAssembly backend run
# through wasmtime's Cranelift JIT (issue #1382): measured ~9x the
# interpreter's throughput across MAGDA's effects with bit-identical output,
# no LLVM dependency, and sandboxed execution so a bad or generated DSP cannot
# take the host down. `interp` remains selectable; `llvm` is pro tier.
set(MAGDA_FAUST_BACKEND "wasm" CACHE STRING "Faust backend: wasm, interp or llvm")
set_property(CACHE MAGDA_FAUST_BACKEND PROPERTY STRINGS "wasm" "interp" "llvm")
set(MAGDA_FAUST_PREBUILT_DIR "" CACHE PATH
    "Directory containing a prebuilt Faust artifact (bin/faust + lib/libfaust); builds from source when empty/missing")

# wasmtime C API — the JIT behind the wasm backend. Bytecode Alliance ships a
# prebuilt per (OS, arch); we pin one per platform, same shape as the ONNX
# Runtime block in the repository root.
#
# Fetched for BOTH branches below. A prebuilt libfaust built with the wasm
# backend still carries undefined wasmtime_* symbols, and the cached artifact
# is only Faust's own bin/ and lib/, so the prebuilt path needs the runtime
# just as much as a source build does.
if(MAGDA_FAUST_BACKEND STREQUAL "wasm")
    set(WASMTIME_VERSION v47.0.2)
    if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
                  OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
        set(WASMTIME_ASSET aarch64-macos-c-api.tar.xz)
        set(WASMTIME_HASH SHA256=c013fe243fd6e13cd63f13dcfa88c3aff5664e356181a2e1bda2c3bf8381fde0)
    elseif(APPLE)
        set(WASMTIME_ASSET x86_64-macos-c-api.tar.xz)
        set(WASMTIME_HASH SHA256=5910124fafa760b8dc3444aae034ba224a7454fe1bb7855d1bed5fc118941588)
    elseif(UNIX AND NOT APPLE)
        # Only x86-64 Linux is a shipping target, but a source build on ARM has
        # to pick the matching asset rather than silently taking the x86-64 one
        # and failing at link time.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            set(WASMTIME_ASSET aarch64-linux-c-api.tar.xz)
            set(WASMTIME_HASH SHA256=8b82df54e4911d2c6bd70804ceb654eed011a24a23c16194f40ba60cc1307d7b)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
            set(WASMTIME_ASSET x86_64-linux-c-api.tar.xz)
            set(WASMTIME_HASH SHA256=35f70f64eb5f9ca72018f3279218a137112c7876b026710315af9ef272a4e91c)
        endif()
    elseif(WIN32)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64|arm64)$")
            set(WASMTIME_ASSET aarch64-windows-c-api.zip)
            set(WASMTIME_HASH SHA256=76668bf6bb45e7940d9f4a844a6f16d26ad3f071f2910f1d3fb5e9d3766e0951)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
            set(WASMTIME_ASSET x86_64-windows-c-api.zip)
            set(WASMTIME_HASH SHA256=80ed88b37de47bbc439f2690aea5f695b85b8bf2c7919556a93803c4f09ffd92)
        endif()
    endif()

    # Catches an unsupported architecture as well as an unsupported OS. The
    # per-OS branches above leave the asset unset rather than guessing, so a
    # riscv64 or armv7 Linux build lands here with a usable message instead of
    # an incompatible archive.
    if(NOT WASMTIME_ASSET)
        message(FATAL_ERROR
            "wasmtime: no prebuilt C API pinned for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR} "
            "(set MAGDA_FAUST_BACKEND=interp to skip)")
    endif()

    FetchContent_Declare(
        wasmtime
        URL      "https://github.com/bytecodealliance/wasmtime/releases/download/${WASMTIME_VERSION}/wasmtime-${WASMTIME_VERSION}-${WASMTIME_ASSET}"
        URL_HASH ${WASMTIME_HASH}
    )
    FetchContent_MakeAvailable(wasmtime)
    add_subdirectory(third_party/wasmtime)
endif()

if(MAGDA_FAUST_PREBUILT_DIR
   AND EXISTS "${MAGDA_FAUST_PREBUILT_DIR}/lib"
   AND EXISTS "${MAGDA_FAUST_PREBUILT_DIR}/bin")
    message(STATUS "Using prebuilt Faust artifact: ${MAGDA_FAUST_PREBUILT_DIR}")
    add_subdirectory(third_party/faust-prebuilt)
else()
    if(MAGDA_FAUST_PREBUILT_DIR)
        message(WARNING "MAGDA_FAUST_PREBUILT_DIR set but artifact missing under it; building Faust from source")
    endif()

    set(INCLUDE_EXECUTABLE ON CACHE BOOL "" FORCE)
    set(INCLUDE_STATIC ON CACHE BOOL "" FORCE)
    set(INCLUDE_DYNAMIC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_OSC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_HTTP OFF CACHE BOOL "" FORCE)
    set(INCLUDE_ITP OFF CACHE BOOL "" FORCE)
    set(INCLUDE_EMCC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_WASM_GLUE OFF CACHE BOOL "" FORCE)
    set(C_BACKEND OFF CACHE STRING "" FORCE)
    set(CPP_BACKEND "COMPILER" CACHE STRING "" FORCE)
    set(CMAJOR_BACKEND OFF CACHE STRING "" FORCE)
    set(CSHARP_BACKEND OFF CACHE STRING "" FORCE)
    set(DLANG_BACKEND OFF CACHE STRING "" FORCE)
    set(FIR_BACKEND OFF CACHE STRING "" FORCE)
    set(JAVA_BACKEND OFF CACHE STRING "" FORCE)
    set(JAX_BACKEND OFF CACHE STRING "" FORCE)
    set(JULIA_BACKEND OFF CACHE STRING "" FORCE)
    set(JSFX_BACKEND OFF CACHE STRING "" FORCE)
    set(OLDCPP_BACKEND OFF CACHE STRING "" FORCE)
    set(RUST_BACKEND OFF CACHE STRING "" FORCE)
    set(TEMPLATE_BACKEND OFF CACHE STRING "" FORCE)
    set(VHDL_BACKEND OFF CACHE STRING "" FORCE)
    set(INTERP_BACKEND OFF CACHE STRING "" FORCE)

    # Exactly one runtime backend is compiled into libfaust. The runtime path
    # (FaustPlugin) picks its factory to match via MAGDA_FAUST_BACKEND_WASM.
    if(MAGDA_FAUST_BACKEND STREQUAL "llvm")
        set(LLVM_BACKEND "STATIC" CACHE STRING "" FORCE)
        set(INTERP_COMP_BACKEND OFF CACHE STRING "" FORCE)
        set(WASM_BACKEND OFF CACHE STRING "" FORCE)
        set(INCLUDE_WASMTIME OFF CACHE BOOL "" FORCE)
    elseif(MAGDA_FAUST_BACKEND STREQUAL "wasm")
        set(LLVM_BACKEND OFF CACHE STRING "" FORCE)
        # The interpreter runtime rides along even though the wasm factory is
        # the one we use. instructions_compiler.cpp includes
        # interpreter_code_container.hh unconditionally, so libfaust always
        # references interpreter_dsp's vtable; GCC and Clang drop it as unused
        # but MSVC requires the definitions and the Windows link fails without
        # them. It also leaves the interpreter available as a fallback.
        set(INTERP_COMP_BACKEND "STATIC" CACHE STRING "" FORCE)
        set(WASM_BACKEND "STATIC" CACHE STRING "" FORCE)
        # WASMTIME_LIB / WASMTIME_INCLUDE_DIR are published by
        # third_party/wasmtime above; libfaust links and includes them
        # directly rather than through a target.
        set(INCLUDE_WASMTIME ON CACHE BOOL "" FORCE)
    else()
        set(LLVM_BACKEND OFF CACHE STRING "" FORCE)
        set(INTERP_COMP_BACKEND "STATIC" CACHE STRING "" FORCE)
        set(WASM_BACKEND OFF CACHE STRING "" FORCE)
        set(INCLUDE_WASMTIME OFF CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(third_party/faust/build third_party/faust EXCLUDE_FROM_ALL)
    add_library(faust::libfaust ALIAS staticlib)

    # libfaust references wasmtime's C API but knows nothing about the static
    # runtime's own system dependencies (CoreFoundation/Security, ws2_32 and
    # friends). Attaching the imported target carries those to the final link.
    if(MAGDA_FAUST_BACKEND STREQUAL "wasm" AND TARGET staticlib)
        target_link_libraries(staticlib PRIVATE wasmtime)
    endif()

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
endif()

# Anything including FaustBackend.hpp must agree with the backend libfaust was
# actually built with -- a mismatch would compile calls to a factory that isn't
# in the binary. Carrying the selection on one INTERFACE target keeps the app,
# the device libraries and the tests from drifting apart.
add_library(magda_faust_backend INTERFACE)
target_include_directories(magda_faust_backend
    INTERFACE ${CMAKE_SOURCE_DIR}/third_party/faust/architecture)
if(MAGDA_FAUST_BACKEND STREQUAL "wasm")
    target_compile_definitions(magda_faust_backend INTERFACE MAGDA_FAUST_BACKEND_WASM=1)
endif()

# Vectorized code generation for the compiled devices (issue #1397).
#
# `-vec` emits no SIMD intrinsics. It splits Faust's single sample loop into
# per-signal sub-loops of MAGDA_FAUST_VECTOR_SIZE samples, a shape the host
# compiler can auto-vectorize at its own baseline ISA. No -march change and no
# new instruction-set requirement, so an older CPU cannot fault on the result.
#
# Whether that helps is per device and per toolchain, which is why it is opt-in
# rather than a default. Measured across macOS/clang, Linux/gcc and
# Windows/MSVC (see tools/faust_vec_bench):
#
#   compressor     +35% / +14% / +34%   <- enabled
#   gate_expander  +29% / +15% / +27%   <- enabled
#   saturator      +22% / +16%  / +3%   <- enabled
#   delay           +9%  / -1% / +15%
#   polysynth      +13%  / -5% / -12%
#   reverb_hall     -2%  / -3%  / -7%
#   filter_svf      -4% / +21% / -21%
#
# The spread is not noise. These DSPs call libm per sample - parameter
# smoothing turns cutoff and gain into per-sample signals - so `-vec` isolates
# each transcendental into its own short loop, and what a toolchain makes of
# that varies wildly. filter_svf swings 42 points between gcc and MSVC on one
# `std::tan` per sample. A device therefore earns this flag by measuring
# positive on all three platforms; it does not get it by default.
set(MAGDA_FAUST_VECTORIZE ON CACHE BOOL
    "Master switch for per-device Faust vectorization; devices still opt in individually")
set(MAGDA_FAUST_VECTOR_SIZE 4 CACHE STRING
    "Faust -vs vector size used by devices that opt into vectorization")

# Add a build-time Faust DSP-to-C++ step and return the generated source path.
# Pass VECTORIZE to generate this device as vectorizable sub-loops; do that
# only with cross-platform benchmark data backing it.
function(magda_compile_faust_dsp DSP_FILE CLASS_NAME OUT_VAR)
    cmake_parse_arguments(ARG "VECTORIZE" "" "" ${ARGN})

    get_filename_component(DSP_NAME "${DSP_FILE}" NAME_WE)
    set(GENERATED_DIR "${CMAKE_BINARY_DIR}/compiled_dsps")
    set(GENERATED_CPP "${GENERATED_DIR}/${DSP_NAME}.generated.cpp")

    set(_magda_faust_codegen_flags -single)
    if(ARG_VECTORIZE AND MAGDA_FAUST_VECTORIZE)
        list(APPEND _magda_faust_codegen_flags -vec -vs ${MAGDA_FAUST_VECTOR_SIZE})
    endif()

    file(MAKE_DIRECTORY "${GENERATED_DIR}")
    add_custom_command(
        OUTPUT "${GENERATED_CPP}"
        COMMAND $<TARGET_FILE:faust>
                -lang cpp
                -cn ${CLASS_NAME}
                -I "${CMAKE_SOURCE_DIR}/third_party/faust/libraries"
                ${_magda_faust_codegen_flags}
                -o "${GENERATED_CPP}"
                "${DSP_FILE}"
        DEPENDS "${DSP_FILE}" $<TARGET_FILE:faust>
        COMMENT "faust → cpp: ${DSP_NAME}.dsp → ${CLASS_NAME}"
        VERBATIM
    )
    set(${OUT_VAR} "${GENERATED_CPP}" PARENT_SCOPE)
endfunction()

# Stage the Faust standard library beside a device-hosting executable so
# runtime-compiled DSP can resolve import("stdfaust.lib").
function(magda_stage_faust_libraries TARGET_NAME)
    get_target_property(_magda_target_is_bundle ${TARGET_NAME} MACOSX_BUNDLE)
    if(APPLE AND _magda_target_is_bundle)
        set(_magda_faust_libraries_destination
            "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET_NAME}>/Resources/faustlibraries")
    else()
        set(_magda_faust_libraries_destination
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/faustlibraries")
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/third_party/faust/libraries"
            "${_magda_faust_libraries_destination}"
        COMMENT "Copying Faust standard libraries for ${TARGET_NAME}"
    )
endfunction()

# Stage the runtime-compiled .dsp library beside a device-hosting executable.
# These are discovered by scanning at startup rather than embedded, so adding
# an effect is a matter of dropping a file in (see FaustResources.cpp).
function(magda_stage_faust_runtime_dsp TARGET_NAME)
    get_target_property(_magda_target_is_bundle ${TARGET_NAME} MACOSX_BUNDLE)
    if(APPLE AND _magda_target_is_bundle)
        set(_magda_faust_dsp_destination
            "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET_NAME}>/Resources/faust_dsp")
    else()
        set(_magda_faust_dsp_destination
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/faust_dsp")
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/magda/daw/audio/faust_dsp/runtime"
            "${_magda_faust_dsp_destination}"
        COMMENT "Copying runtime Faust DSP library for ${TARGET_NAME}"
    )
endfunction()

# Fail configuration when a device pack reaches into host-owned DAW layers.
# Callers pass the pack's complete source list, making this check usable by
# in-tree, submodule, and private packs alike.
function(magda_validate_device_pack_sources PACK_NAME)
    set(_magda_pack_sources ${ARGN})
    set(_magda_enforce_engine_neutral FALSE)
    list(FIND _magda_pack_sources "ENGINE_NEUTRAL" _magda_engine_neutral_index)
    if(NOT _magda_engine_neutral_index EQUAL -1)
        set(_magda_enforce_engine_neutral TRUE)
        list(REMOVE_AT _magda_pack_sources ${_magda_engine_neutral_index})
    endif()

    foreach(_magda_pack_source IN LISTS _magda_pack_sources)
        if(NOT IS_ABSOLUTE "${_magda_pack_source}")
            set(_magda_pack_source "${CMAKE_CURRENT_SOURCE_DIR}/${_magda_pack_source}")
        endif()
        if(NOT EXISTS "${_magda_pack_source}"
           OR NOT _magda_pack_source MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$")
            continue()
        endif()

        file(READ "${_magda_pack_source}" _magda_pack_source_text)
        if(_magda_enforce_engine_neutral
           AND (_magda_pack_source_text MATCHES
                "#[ \t]*include[ \t]*[<\"][^>\"]*tracktion[^>\"]*[>\"]"
                OR _magda_pack_source_text MATCHES
                "(^|[^A-Za-z0-9_])(tracktion::|te::)"))
            message(FATAL_ERROR
                "${PACK_NAME} names Tracktion instead of the MagdaDevice contract: "
                "${_magda_pack_source}")
        endif()

        if(_magda_pack_source_text MATCHES "(TrackManager|Config)::getInstance[ \t]*\\(")
            message(FATAL_ERROR
                "${PACK_NAME} calls a DAW singleton instead of DeviceServices: "
                "${_magda_pack_source}")
        endif()

        string(REGEX MATCHALL
            "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]"
            _magda_pack_includes "${_magda_pack_source_text}")
        foreach(_magda_pack_include IN LISTS _magda_pack_includes)
            if(_magda_pack_include MATCHES
               "[<\"]([^>\"]*/)?(engine|project|ui|plugin_manager|racks|modifiers)/[^>\"]+[>\"]")
                message(FATAL_ERROR
                    "${PACK_NAME} crosses the device SDK include boundary: "
                    "${_magda_pack_source}\n${_magda_pack_include}")
            endif()
            if(_magda_pack_include MATCHES
               "[<\"]([^>\"]*/)?session/SessionClipAudioMonitor\\.hpp[>\"]")
                message(FATAL_ERROR
                    "${PACK_NAME} includes the host session monitor instead of DeviceServices: "
                    "${_magda_pack_source}\n${_magda_pack_include}")
            endif()

            if(_magda_pack_include MATCHES "[<\"]([^>\"]*/)?core/([^>\"]+)[>\"]")
                set(_magda_pack_core_header "${CMAKE_MATCH_2}")
                if(NOT _magda_pack_core_header MATCHES
                   "^(TypeIds|TechnicalText|ChainNodePath|ControlTarget|ParameterInfo|ParameterUtils|DeviceInfo|KitRow|MacroInfo|ModInfo|TempoUtils)\\.hpp$")
                    message(FATAL_ERROR
                        "${PACK_NAME} includes a non-SDK core header: "
                        "${_magda_pack_source}\n${_magda_pack_include}")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()

# Mutable Instruments DSP (Elements / Rings / Clouds) native ports.
set(MAGDA_MUTABLE_PREBUILT_DIR "" CACHE PATH
    "Directory containing a prebuilt magda_mutable artifact (lib/libmagda_mutable); builds from source when empty/missing")

if(MAGDA_MUTABLE_PREBUILT_DIR AND EXISTS "${MAGDA_MUTABLE_PREBUILT_DIR}/lib")
    message(STATUS "Using prebuilt Mutable artifact: ${MAGDA_MUTABLE_PREBUILT_DIR}")
    add_subdirectory(third_party/mutable-prebuilt)
else()
    if(MAGDA_MUTABLE_PREBUILT_DIR)
        message(WARNING "MAGDA_MUTABLE_PREBUILT_DIR set but artifact missing under it; building Mutable from source")
    endif()

    set(MI_ROOT ${CMAKE_SOURCE_DIR}/third_party/eurorack)
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
        target_compile_options(magda_mutable PRIVATE /wd4305 /wd4244 /wd4267)
        set_source_files_properties(
            ${MI_ROOT}/elements/resources.cc
            ${MI_ROOT}/rings/resources.cc
            ${MI_ROOT}/clouds/resources.cc
            TARGET_DIRECTORY magda_mutable
            PROPERTIES COMPILE_OPTIONS "/Od")
    else()
        target_compile_options(magda_mutable PRIVATE -w)
    endif()
    set_target_properties(magda_mutable PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${MI_ROOT}/build/lib")
    add_library(magda::mutable ALIAS magda_mutable)
endif()
