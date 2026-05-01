# Audio thread, plugin hosting, and routing layer.
# Most of these compile into magda_daw; AudioEngineOptimizer is app-only.

target_sources(magda_daw PRIVATE
    AudioBridge.cpp
    AudioThumbnailManager.cpp
    WaveformPeakCache.cpp
    DeviceMeteringManager.cpp
    processors/DeviceProcessor.cpp
    MidiBridge.cpp
    midi/MidiDeviceMatch.cpp
    midi/QwertyMidiKeyboard.cpp
    session/SessionClipScheduler.cpp
    session/SessionClipAudioMonitor.cpp
    session/SessionMonitorPlugin.cpp
    session/SessionRecorder.cpp
    # Phase 1: Pure data managers
    transport/TransportStateManager.cpp
    midi/MidiActivityMonitor.cpp
    params/ParameterManager.cpp
    # Phase 2: Independent features
    PluginWindowBridge.cpp
    WarpMarkerManager.cpp
    # Phase 3: Core controllers
    TrackController.cpp
    plugin_manager/PluginManager.cpp
    plugin_manager/PluginManagerSync.cpp
    plugin_manager/PluginManagerMacros.cpp
    plugin_manager/PluginManagerModifiers.cpp
    plugin_manager/PluginManagerSidechain.cpp
    racks/InstrumentRackManager.cpp
    racks/RackSyncManager.cpp
    modifiers/ModifierSync.cpp
    automation/AutomationPlaybackEngine.cpp
    automation/AutomationRecordingEngine.cpp
    automation/AutomationCurveSimplifier.cpp
    session/ClipSynchronizer.cpp
    plugins/SidechainMonitorPlugin.cpp
    plugins/AudioSidechainMonitorPlugin.cpp
    plugins/MidiReceivePlugin.cpp
    plugins/MagdaSamplerPlugin.cpp
    plugins/DrumGridPlugin.cpp
    plugins/MidiChordEnginePlugin.cpp
    plugins/ArpeggiatorPlugin.cpp
    plugins/MidiDevicePlugin.cpp
    transport/StepClock.cpp
    plugins/StepSequencerPlugin.cpp
    # Controller routing (issue #1050 -- Phase C)
    controllers/ControllerParamWriter.cpp
    controllers/ControllerRouter.cpp
    # Headers (listed for IDE visibility)
    AudioBridge.hpp
    session/SessionClipScheduler.hpp
    session/SessionClipAudioMonitor.hpp
    session/SessionClipCommandQueue.hpp
    session/SessionClipStateQueue.hpp
    session/SessionMonitorPlugin.hpp
    MeteringBuffer.hpp
    params/ParameterQueue.hpp
    transport/TransportStateManager.hpp
    midi/MidiActivityMonitor.hpp
    params/ParameterManager.hpp
    PluginWindowBridge.hpp
    WarpMarkerManager.hpp
    TrackController.hpp
    plugin_manager/PluginManager.hpp
    racks/InstrumentRackManager.hpp
    racks/RackSyncManager.hpp
    automation/AutomationPlaybackEngine.hpp
    session/ClipSynchronizer.hpp
    plugins/SidechainTriggerBus.hpp
    plugins/SidechainMonitorPlugin.hpp
    plugins/AudioSidechainMonitorPlugin.hpp
    midi/MidiBroadcastBus.hpp
    plugins/MidiReceivePlugin.hpp
    plugins/MagdaSamplerPlugin.hpp
    plugins/DrumGridPlugin.hpp
    plugins/MidiChordEnginePlugin.hpp
    plugins/ArpeggiatorPlugin.hpp
    plugins/MidiDevicePlugin.hpp
    transport/StepClock.hpp
    plugins/StepSequencerPlugin.hpp
)

target_sources(magda_daw_app PRIVATE
    AudioEngineOptimizer.cpp
    AudioEngineOptimizer.hpp
)
