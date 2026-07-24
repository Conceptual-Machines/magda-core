# Build dependencies owned by the device layer. This module is included by the
# repository root before magda_devices is declared.

# Faust — runtime DSP compilation. Free tier uses the interpreter backend (no
# LLVM, smaller binary); the pro tier can select the LLVM JIT backend.
set(MAGDA_FAUST_BACKEND "interp" CACHE STRING "Faust backend: interp or llvm")
set_property(CACHE MAGDA_FAUST_BACKEND PROPERTY STRINGS "interp" "llvm")
set(MAGDA_FAUST_PREBUILT_DIR "" CACHE PATH
    "Directory containing a prebuilt Faust artifact (bin/faust + lib/libfaust); builds from source when empty/missing")

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
    set(INCLUDE_WASMTIME OFF CACHE BOOL "" FORCE)
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
    set(WASM_BACKEND OFF CACHE STRING "" FORCE)
    set(INTERP_BACKEND OFF CACHE STRING "" FORCE)

    if(MAGDA_FAUST_BACKEND STREQUAL "llvm")
        set(LLVM_BACKEND "STATIC" CACHE STRING "" FORCE)
        set(INTERP_COMP_BACKEND OFF CACHE STRING "" FORCE)
    else()
        set(LLVM_BACKEND OFF CACHE STRING "" FORCE)
        set(INTERP_COMP_BACKEND "STATIC" CACHE STRING "" FORCE)
    endif()

    add_subdirectory(third_party/faust/build third_party/faust EXCLUDE_FROM_ALL)
    add_library(faust::libfaust ALIAS staticlib)

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
