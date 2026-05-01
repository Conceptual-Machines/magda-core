# Tracktion Engine wrapper and out-of-process plugin scanner.
# magda_plugin_scanner uses plugin_scanner_main.cpp directly via the
# parent CMakeLists; everything else is library code linked into magda_daw.

target_sources(magda_daw PRIVATE
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
    PluginScanner.cpp
    PluginScanCoordinator.cpp
    ScanWorker.cpp
    PluginWindowManager.cpp
    # Headers (listed for IDE visibility)
    AudioEngine.hpp
    TracktionEngineWrapper.hpp
    MagdaUIBehaviour.hpp
)

target_sources(magda_daw_app PRIVATE
    PlaybackPositionTimer.cpp
    PlaybackPositionTimer.hpp
)
