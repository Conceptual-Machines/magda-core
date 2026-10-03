# JUCE-free MAGDA devices (#2940): built into the app, and through the SDK hosts into the
# parity renderer, a JUCE plugin and a wasm module. Configures on its own for Emscripten:
#   emcmake cmake -S magda/devices -B build-wasm-devices
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(magda_portable_devices LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_subdirectory(../../third_party/magda-sdk magda-sdk)
endif()

add_library(magda_portable_devices STATIC
    faust/CompiledEffect.cpp
    faust/FaustMetadataParser.cpp
    faust/effects/Bitcrusher.cpp
    faust/effects/Chorus.cpp
    faust/effects/Clipper.cpp
    faust/effects/Compressor.cpp
    faust/effects/Delay.cpp
    faust/effects/Dimension.cpp
    faust/effects/Eq.cpp
    faust/effects/Filter.cpp
    faust/effects/Flanger.cpp
    faust/effects/FreqShift.cpp
    faust/effects/GateExpander.cpp
    faust/effects/GrainDelay.cpp
    faust/effects/Grit.cpp
    faust/effects/Limiter.cpp
    faust/effects/Mod.cpp
    faust/effects/Multiband.cpp
    faust/effects/Phaser.cpp
    faust/effects/Pitch.cpp
    faust/effects/Reverb.cpp
    faust/effects/RingMod.cpp
    faust/effects/Saturator.cpp
    faust/effects/Utility.cpp
    tone/ToneGenerator.cpp)
target_include_directories(magda_portable_devices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(magda_portable_devices SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/faust/architecture")
target_link_libraries(magda_portable_devices PUBLIC magda::sdk_core)

# The effects #include the generated dsps. In the app build they come from magda/daw/audio's
# compile step; a standalone configure (wasm) reads them from a native build's compiled_dsps.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    target_include_directories(magda_portable_devices SYSTEM PRIVATE "${CMAKE_BINARY_DIR}/compiled_dsps")
    add_dependencies(magda_portable_devices magda_compiled_dsps)
else()
    set(MAGDA_COMPILED_DSPS_DIR "" CACHE PATH "A native build's compiled_dsps directory")
    if(NOT MAGDA_COMPILED_DSPS_DIR)
        message(FATAL_ERROR "Set MAGDA_COMPILED_DSPS_DIR to a native build's compiled_dsps")
    endif()
    target_include_directories(magda_portable_devices SYSTEM PRIVATE "${MAGDA_COMPILED_DSPS_DIR}")
endif()

magda_sdk_add_device_module(magda_portable
    SOURCES PortableModule.cpp
    LIBRARIES magda_portable_devices)

# Built on request (`make device-plugins`).
if(COMMAND juce_add_plugin)
    magda_sdk_add_juce_plugin(magda_test_tone_plugin
        MODULE magda_portable
        DEVICE toneGenerator
        EXCLUDE_FROM_ALL
        PRODUCT_NAME "MAGDA Test Tone"
        COMPANY_NAME "Conceptual Machines"
        BUNDLE_ID com.conceptualmachines.magda.testtone
        PLUGIN_MANUFACTURER_CODE Cmgd
        PLUGIN_CODE Mtt1
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE  # an instrument AU must take MIDI; the ABI drops what the device ignores
        NEEDS_MIDI_OUTPUT FALSE
        FORMATS VST3 AU Standalone)

    magda_sdk_add_juce_plugin(magda_delay_plugin
        MODULE magda_portable
        DEVICE magda_delay
        EXCLUDE_FROM_ALL
        PRODUCT_NAME "MAGDA Delay"
        COMPANY_NAME "Conceptual Machines"
        BUNDLE_ID com.conceptualmachines.magda.delay
        PLUGIN_MANUFACTURER_CODE Cmgd
        PLUGIN_CODE Mdl1
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        FORMATS VST3 AU Standalone)
endif()
