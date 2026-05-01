# Audio thread, plugin hosting, and routing layer.
# Most of these compile into magda_daw; AudioEngineOptimizer is app-only.

target_sources(magda_daw PRIVATE
    AudioBridge.cpp
    AudioThumbnailManager.cpp
    DeviceMeteringManager.cpp
    DeviceProcessor.cpp
    MidiBridge.cpp
    MidiDeviceMatch.cpp
    QwertyMidiKeyboard.cpp
    SessionClipScheduler.cpp
    SessionClipAudioMonitor.cpp
    SessionMonitorPlugin.cpp
    SessionRecorder.cpp
    # Phase 1: Pure data managers
    TransportStateManager.cpp
    MidiActivityMonitor.cpp
    ParameterManager.cpp
    # Phase 2: Independent features
    PluginWindowBridge.cpp
    WarpMarkerManager.cpp
    # Phase 3: Core controllers
    TrackController.cpp
    PluginManager/PluginManager.cpp
    PluginManager/PluginManagerSync.cpp
    PluginManager/PluginManagerMacros.cpp
    PluginManager/PluginManagerModifiers.cpp
    PluginManager/PluginManagerSidechain.cpp
    InstrumentRackManager.cpp
    RackSyncManager.cpp
    ModifierSync.cpp
    AutomationPlaybackEngine.cpp
    AutomationRecordingEngine.cpp
    AutomationCurveSimplifier.cpp
    ClipSynchronizer.cpp
    SidechainMonitorPlugin.cpp
    AudioSidechainMonitorPlugin.cpp
    MidiReceivePlugin.cpp
    MagdaSamplerPlugin.cpp
    DrumGridPlugin.cpp
    MidiChordEnginePlugin.cpp
    ArpeggiatorPlugin.cpp
    MidiDevicePlugin.cpp
    StepClock.cpp
    StepSequencerPlugin.cpp
    # Controller routing (issue #1050 -- Phase C)
    ControllerParamWriter.cpp
    ControllerRouter.cpp
    # Headers (listed for IDE visibility)
    AudioBridge.hpp
    SessionClipScheduler.hpp
    SessionClipAudioMonitor.hpp
    SessionClipCommandQueue.hpp
    SessionClipStateQueue.hpp
    SessionMonitorPlugin.hpp
    MeteringBuffer.hpp
    ParameterQueue.hpp
    TransportStateManager.hpp
    MidiActivityMonitor.hpp
    ParameterManager.hpp
    PluginWindowBridge.hpp
    WarpMarkerManager.hpp
    TrackController.hpp
    PluginManager/PluginManager.hpp
    InstrumentRackManager.hpp
    RackSyncManager.hpp
    AutomationPlaybackEngine.hpp
    ClipSynchronizer.hpp
    SidechainTriggerBus.hpp
    SidechainMonitorPlugin.hpp
    AudioSidechainMonitorPlugin.hpp
    MidiBroadcastBus.hpp
    MidiReceivePlugin.hpp
    MagdaSamplerPlugin.hpp
    DrumGridPlugin.hpp
    MidiChordEnginePlugin.hpp
    ArpeggiatorPlugin.hpp
    MidiDevicePlugin.hpp
    StepClock.hpp
    StepSequencerPlugin.hpp
)

target_sources(magda_daw_app PRIVATE
    AudioEngineOptimizer.cpp
    AudioEngineOptimizer.hpp
)
