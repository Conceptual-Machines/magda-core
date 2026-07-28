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
        set(WASMTIME_ASSET x86_64-linux-c-api.tar.xz)
        set(WASMTIME_HASH SHA256=35f70f64eb5f9ca72018f3279218a137112c7876b026710315af9ef272a4e91c)
    elseif(WIN32)
        set(WASMTIME_ASSET x86_64-windows-c-api.zip)
        set(WASMTIME_HASH SHA256=80ed88b37de47bbc439f2690aea5f695b85b8bf2c7919556a93803c4f09ffd92)
    else()
        message(FATAL_ERROR
            "wasmtime: no prebuilt C API for this platform "
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

# Add a build-time Faust DSP-to-C++ step and return the generated source path.
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

# Fail configuration when a device pack reaches into host-owned DAW layers.
# Callers pass the pack's complete source list, making this check usable by
# in-tree, submodule, and private packs alike.
function(magda_validate_device_pack_sources PACK_NAME)
    foreach(_magda_pack_source IN LISTS ARGN)
        if(NOT IS_ABSOLUTE "${_magda_pack_source}")
            set(_magda_pack_source "${CMAKE_CURRENT_SOURCE_DIR}/${_magda_pack_source}")
        endif()
        if(NOT EXISTS "${_magda_pack_source}"
           OR NOT _magda_pack_source MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$")
            continue()
        endif()

        file(READ "${_magda_pack_source}" _magda_pack_source_text)
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
