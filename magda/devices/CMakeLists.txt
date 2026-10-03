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

add_library(magda_portable_devices STATIC tone/ToneGenerator.cpp)
target_include_directories(magda_portable_devices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(magda_portable_devices PUBLIC magda::sdk_core)

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
endif()
