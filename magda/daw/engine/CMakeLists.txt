# Tracktion Engine wrapper and out-of-process plugin scanner.
# magda_plugin_scanner uses plugin_scanner_main.cpp directly via the
# parent CMakeLists; everything else is library code linked into magda_daw.

target_sources(magda_daw PRIVATE
    TempoLaneBridge.cpp
    TempoLaneSync.cpp
    TempoSequenceRippleCommand.cpp
    OfflineRenderHelper.cpp
    AudioEngineChoice.cpp
    TracktionEngineWrapper.cpp
    TracktionEngineWrapperInit.cpp
    TracktionEngineWrapperDevices.cpp
    TracktionEngineWrapperTransport.cpp
    TracktionEngineWrapperTracks.cpp
    TracktionEngineWrapperClips.cpp
    TracktionEngineWrapperRecording.cpp
    TracktionEngineWrapperPlugins.cpp
    MagdaUIBehaviour.cpp
    PluginExclusions.cpp
    PluginMetadataStore.cpp
    PluginScanner.cpp
    PluginScanCoordinator.cpp
    ScanWorker.cpp
    PluginWindowManager.cpp
    # Headers (listed for IDE visibility)
    AudioEngine.hpp
    AudioEngineChoice.hpp
    OfflineRenderHelper.hpp
    TracktionEngineWrapper.hpp
    MagdaUIBehaviour.hpp
)

target_sources(magda_daw_app PRIVATE
    PlaybackPositionTimer.cpp
    PlaybackPositionTimer.hpp
)

# The app's end of magda::engine (#2551), and the first thing outside the test
# binaries to link it. Its own target for the reason magda_engine_devices is
# one: it names both the engine's headers and the app's, and magda_daw sees
# neither of the engine's. MagdaAudioEngine reaches it through one pimpl'd
# header, which is all that crosses that line.
if(MAGDA_BUILD_NATIVE_ENGINE)
    # MagdaAudioEngine is only a choice while there is a second engine to
    # choose. Without one it would be a class whose whole reason to exist was
    # not built, so it is compiled on the same switch rather than left as a
    # shell that forwards everything.
    target_sources(magda_daw PRIVATE MagdaAudioEngine.cpp MagdaAudioEngine.hpp)

    add_library(magda_engine_host STATIC
        host/EngineHost.cpp
        host/EngineProject.cpp
        host/EngineRuntimeFactory.cpp
        host/EngineTrace.cpp
        host/ExternalPluginLoader.cpp
        host/LiveMidiQueue.cpp
        host/LiveMidiSources.cpp
        host/EngineHost.hpp
        host/EngineProject.hpp
        host/EngineRuntimeFactory.hpp
        host/EngineTrace.hpp
        host/ExternalPluginLoader.hpp
        host/LiveMidiQueue.hpp
        host/LiveMidiSources.hpp
    )
    add_library(magda::engine_host ALIAS magda_engine_host)

    target_compile_features(magda_engine_host PRIVATE cxx_std_23)
    target_include_directories(magda_engine_host PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/magda/daw
        ${CMAKE_SOURCE_DIR}/magda/daw/audio
        # ProjectInfo.hpp reaches the configured version.hpp, and the host
        # follows the project lifecycle (#2576).
        ${CMAKE_BINARY_DIR}/magda/daw/generated
    )

    # magda_engine is added after magda/daw, so this names it before it exists.
    # A plain target name resolves at generate time, which is what makes that
    # legal -- the same arrangement magda_engine_devices has.
    target_link_libraries(magda_engine_host
        PUBLIC
        magda_engine
        magda_engine_devices
    )

    # JUCE headers and defines without the modules: the callback is a
    # juce::AudioIODeviceCallback and the reader factory an
    # AudioFormatManager, and both modules are already compiled once in
    # magda_daw, so the symbols resolve at the final link.
    foreach(_juce_module IN ITEMS
            juce::juce_events
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_audio_processors)
        target_include_directories(magda_engine_host PRIVATE
            $<TARGET_PROPERTY:${_juce_module},INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(magda_engine_host PRIVATE
            $<TARGET_PROPERTY:${_juce_module},INTERFACE_COMPILE_DEFINITIONS>)
    endforeach()
endif()

# --- One construction site ----------------------------------------------------
#
# Which engine renders is decided in createDefaultAudioEngine, and that is worth
# nothing if a caller can name an implementation instead. magda_daw_main.cpp did
# exactly that, so #2558's seam and #2551's engine were both switchable
# everywhere except in the running app, and the environment variable looked
# wired while doing nothing.
#
# Checked at configure time rather than left to review, the way the native
# engine's own dependency boundary is (magda/engine/CMakeLists.txt).
#
# The engine directory itself is exempt: the factory is here, and so is
# MagdaAudioEngine, which holds a fork of its own by design. Tests are exempt
# because a test that wants one engine specifically is asking a different
# question from an application that must not care.
file(GLOB_RECURSE MAGDA_ENGINE_CALLER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../*.cpp")

foreach(_caller_source ${MAGDA_ENGINE_CALLER_SOURCES})
    file(RELATIVE_PATH _caller_relative "${CMAKE_SOURCE_DIR}" "${_caller_source}")

    if(_caller_relative MATCHES "^magda/daw/engine/")
        continue()
    endif()

    file(STRINGS "${_caller_source}" _engine_construction
        REGEX "(make_unique|make_shared)<[ \t]*(magda::)?(TracktionEngineWrapper|MagdaAudioEngine)")

    if(_engine_construction)
        message(FATAL_ERROR
            "\n"
            "Audio engine constructed directly in ${_caller_relative}:\n"
            "    ${_engine_construction}\n"
            "Which engine renders is createDefaultAudioEngine's to decide (#2551).\n"
            "Naming an implementation here makes that choice unreachable.\n")
    endif()
endforeach()
