# Tracktion Engine wrapper and out-of-process plugin scanner.
# magda_plugin_scanner uses plugin_scanner_main.cpp directly via the
# parent CMakeLists; everything else is library code linked into magda_daw.

target_sources(magda_daw PRIVATE
    TempoLaneBridge.cpp
    TempoLaneSync.cpp
    TempoSequenceRippleCommand.cpp
    OfflineRenderHelper.cpp
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
        host/EngineHost.hpp
        host/EngineProject.hpp
        host/EngineRuntimeFactory.hpp
        host/EngineTrace.hpp
    )
    add_library(magda::engine_host ALIAS magda_engine_host)

    target_compile_features(magda_engine_host PRIVATE cxx_std_23)
    target_include_directories(magda_engine_host PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/magda/daw
        ${CMAKE_SOURCE_DIR}/magda/daw/audio
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
