# DAW Domain - Digital Audio Workstation Core + UI
# Simple version focusing on Tracktion Engine integration

# Generate version header from project version
configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp" @ONLY)

# Create binary data for fonts and icons
juce_add_binary_data(MagdaAssets
    SOURCES
    # Fonts
    ../../assets/fonts/Inter-Regular.ttf
    ../../assets/fonts/Inter-Medium.ttf
    ../../assets/fonts/Inter-SemiBold.ttf
    ../../assets/fonts/Inter-Bold.ttf
    "../../assets/fonts/Microgramma D Extended Bold.otf"
    ../../assets/fonts/JetBrainsMono-Regular.ttf
    # General UI icons
    ../../assets/icons/general/enter.svg
    ../../assets/icons/general/track.svg
    ../../assets/icons/general/clip.svg
    ../../assets/icons/general/copy.svg
    ../../assets/icons/general/copycontent.svg
    ../../assets/icons/general/delete.svg
    ../../assets/icons/general/random.svg
    ../../assets/icons/general/compare.svg
    ../../assets/icons/general/add.svg
    ../../assets/icons/general/settings_nobg.svg
    ../../assets/icons/general/menu.svg
    ../../assets/icons/general/chord.svg
    ../../assets/icons/general/funnel.svg
    ../../assets/icons/general/learn.svg
    ../../assets/icons/general/refresh.svg
    ../../assets/icons/general/send.svg
    ../../assets/icons/general/chevron_left.svg
    ../../assets/icons/general/chevron_right.svg
    ../../assets/icons/general/chevron_down.svg
    ../../assets/icons/general/bar_chart.svg
    ../../assets/icons/general/link_flat.svg
    ../../assets/icons/general/play_bare.svg
    ../../assets/icons/general/server_play.svg
    ../../assets/icons/general/server_stop.svg
    ../../assets/icons/general/open_in_new.svg
    ../../assets/icons/general/power_on.svg
    ../../assets/icons/general/rack.svg
    ../../assets/icons/general/tree.svg
    ../../assets/icons/general/save.svg
    ../../assets/icons/general/folder-open.svg
    ../../assets/icons/general/piano_roll.svg
    ../../assets/icons/general/drum_grid.svg
    ../../assets/icons/general/script.svg
    ../../assets/icons/general/volume_up.svg
    ../../assets/icons/general/volume_off.svg
    ../../assets/icons/general/headphones.svg
    ../../assets/icons/general/metronome.svg
    ../../assets/icons/general/project_home.svg
    ../../assets/icons/general/harddrive.svg
    ../../assets/icons/general/database.svg
    ../../assets/icons/general/favorite.svg
    # Transport icons
    ../../assets/icons/transport/play_off.svg
    ../../assets/icons/transport/play_on.svg
    ../../assets/icons/transport/pause_off.svg
    ../../assets/icons/transport/pause_on.svg
    ../../assets/icons/transport/stop_off.svg
    ../../assets/icons/transport/stop_on.svg
    ../../assets/icons/transport/record_off.svg
    ../../assets/icons/transport/record_on.svg
    ../../assets/icons/transport/record_circle.svg
    ../../assets/icons/transport/loop_off.svg
    ../../assets/icons/transport/loop_on.svg
    ../../assets/icons/transport/loop_icon.svg
    ../../assets/icons/transport/rewind_off.svg
    ../../assets/icons/transport/rewind_on.svg
    ../../assets/icons/transport/prev_off.svg
    ../../assets/icons/transport/prev_on.svg
    ../../assets/icons/transport/next_off.svg
    ../../assets/icons/transport/next_on.svg
    ../../assets/icons/transport/punchin.svg
    ../../assets/icons/transport/punchin_on.svg
    ../../assets/icons/transport/punchout.svg
    ../../assets/icons/transport/punchout_on.svg
    ../../assets/icons/transport/automation_off.svg
    ../../assets/icons/transport/automation_on.svg
    ../../assets/icons/transport/midi_qwerty_off.svg
    ../../assets/icons/transport/midi_qwerty_on.svg
    # Inspector icons
    ../../assets/icons/inspector/infinito.svg
    ../../assets/icons/inspector/position.svg
    ../../assets/icons/inspector/clip_loop_off.svg
    ../../assets/icons/inspector/clip_loop_on.svg
    ../../assets/icons/inspector/audio_clip.svg
    ../../assets/icons/inspector/midi_clip.svg
    ../../assets/icons/inspector/fade_linear.svg
    ../../assets/icons/inspector/fade_convex.svg
    ../../assets/icons/inspector/fade_concave.svg
    ../../assets/icons/inspector/fade_scurve.svg
    ../../assets/icons/inspector/fade_gain.svg
    ../../assets/icons/inspector/fade_speedramp.svg
    ../../assets/icons/inspector/phase_invert.svg
    # Resume/arrangement icons
    ../../assets/icons/resume/resume.svg
    ../../assets/icons/resume/resume_on.svg
    # Automation icons
    ../../assets/icons/automation/horizontal_snap.svg
    ../../assets/icons/automation/vertical_snap.svg
    ../../assets/icons/automation/automation.svg
    # Waveform icons
    ../../assets/icons/waveform/sine-wave.svg
    ../../assets/icons/waveform/bare_sine.svg
    ../../assets/icons/waveform/time_bend.svg
    ../../assets/icons/waveform/time_bend_off.svg
    ../../assets/icons/waveform/time_bend_on.svg
    ../../assets/icons/waveform/bezier.svg
    ../../assets/icons/waveform/bend-in-out.svg
    ../../assets/icons/waveform/curve.svg
    # Toolbar icons
    ../../assets/icons/toolbar/density_small.svg
    ../../assets/icons/toolbar/density_medium.svg
    ../../assets/icons/toolbar/density_large.svg
    ../../assets/icons/toolbar/zoom_out_map.svg
    ../../assets/icons/toolbar/fit_width.svg
    ../../assets/icons/toolbar/fit_loop.svg
    ../../assets/icons/toolbar/horizontal.svg
    ../../assets/icons/toolbar/vertical.svg
    ../../assets/icons/toolbar/io_routing.svg
    # Footer view mode icons
    ../../assets/icons/footer/Session.svg
    ../../assets/icons/footer/Arrangement.svg
    ../../assets/icons/footer/Mix.svg
    ../../assets/icons/footer/Master.svg
    # Sidebar icons
    ../../assets/icons/sidebar/plug.svg
    ../../assets/icons/sidebar/sample.svg
    ../../assets/icons/sidebar/preset.svg
    ../../assets/icons/sidebar/info.svg
    ../../assets/icons/sidebar/console.svg
    # Browser icons
    ../../assets/icons/browser/browser.svg
    ../../assets/icons/browser/AUDIO-DEVICE.svg
    ../../assets/icons/browser/INSTRUMENT_DEVICE.svg
    ../../assets/icons/browser/MIDI-DEVICE.svg
    ../../assets/midi.svg
    ../../assets/icons/browser/AUDIO.svg
    ../../assets/icons/browser/MIDI.svg
    # Mixer fader icons
    ../../assets/icons/fader/fader_thumb.svg
    ../../assets/icons/fader/fader_track.svg
    # Mixer knob icons
    ../../assets/icons/knob/knob.svg
    ../../assets/icons/knob/knob_body.svg
    ../../assets/icons/knob/knob_pointer.svg
    # I/O routing icons
    ../../assets/icons/IO/Input.svg
    ../../assets/icons/IO/Output.svg
    ../../assets/icons/IO/multiout.svg
    # Panel open/close icons
    ../../assets/icons/panels/left_open.svg
    ../../assets/icons/panels/left_close.svg
    ../../assets/icons/panels/right_open.svg
    ../../assets/icons/panels/right_close.svg
    ../../assets/icons/panels/bottom_open.svg
    ../../assets/icons/panels/bottom_close.svg
    # Fontaudio waveform icons
    ../../assets/icons/fontaudio/fad-modsine.svg
    ../../assets/icons/fontaudio/fad-modtri.svg
    ../../assets/icons/fontaudio/fad-modsawup.svg
    ../../assets/icons/fontaudio/fad-modsquare.svg
    ../../assets/icons/fontaudio/fad-modrandom.svg
    # Fontaudio filter icons
    ../../assets/icons/fontaudio/fad-filter-bypass.svg
    ../../assets/icons/fontaudio/fad-filter-lowpass.svg
    ../../assets/icons/fontaudio/fad-filter-highpass.svg
    ../../assets/icons/fontaudio/fad-filter-bandpass.svg
    ../../assets/icons/fontaudio/fad-filter-notch.svg
    # Fontaudio logo icons
    ../../assets/icons/fontaudio/fad-logo-tracktion.svg
    ../../assets/icons/fontaudio/fad-logo-juce.svg
    # AI icons
    ../../assets/icons/ai/psychology.svg
    ../../assets/icons/ai/robot.svg
    ../../assets/icons/ai/ai.svg
    # Cloud provider icons
    ../../assets/icons/cloud_providers/openai.svg
    ../../assets/icons/cloud_providers/anthropic.svg
    ../../assets/icons/cloud_providers/gemini.svg
    ../../assets/icons/cloud_providers/deepseek.svg
    ../../assets/icons/cloud_providers/openrouter.svg
    # App logo
    ../../assets/magda-lisa.svg
)

# DAW Core sources
set(DAW_CORE_SOURCES
    command.cpp
    magda.cpp
    core/Config.cpp
    core/ViewModeController.cpp
    core/TrackManager.cpp
    core/TrackManagerModulation.cpp
    core/TrackManagerDevices.cpp
    core/ModulatorEngine.cpp
    core/ClipManager.cpp
    core/MidiFileWriter.cpp
    core/SelectionManager.cpp
    core/AutomationInfo.cpp
    core/AutomationManager.cpp
    core/LinkModeManager.cpp
    core/UndoManager.cpp
    core/ClipCommands.cpp
    core/ClipPropertyCommands.cpp
    core/WarpMarkerCommands.cpp
    core/TrackCommands.cpp
    core/TrackPropertyCommands.cpp
    core/AutomationCommands.cpp
    core/MidiNoteCommands.cpp
    core/ParameterInfo.cpp
    core/ParameterUtils.cpp
    core/StringTable.cpp
    core/ParameterDetector.cpp
    engine/TracktionEngineWrapper.cpp
    engine/TracktionEngineWrapperInit.cpp
    engine/TracktionEngineWrapperDevices.cpp
    engine/TracktionEngineWrapperTransport.cpp
    engine/TracktionEngineWrapperTracks.cpp
    engine/TracktionEngineWrapperClips.cpp
    engine/TracktionEngineWrapperRecording.cpp
    engine/TracktionEngineWrapperPlugins.cpp
    engine/MagdaUIBehaviour.cpp
    engine/PluginExclusions.cpp
    engine/PluginScanner.cpp
    engine/PluginScanCoordinator.cpp
    engine/ScanWorker.cpp
    engine/PluginWindowManager.cpp
    # Audio integration
    audio/AudioBridge.cpp
    audio/AudioThumbnailManager.cpp
    audio/DeviceMeteringManager.cpp
    audio/DeviceProcessor.cpp
    audio/MidiBridge.cpp
    audio/QwertyMidiKeyboard.cpp
    audio/SessionClipScheduler.cpp
    audio/SessionClipAudioMonitor.cpp
    audio/SessionMonitorPlugin.cpp
    audio/SessionRecorder.cpp
    # Phase 1 refactoring: Pure data managers
    audio/TransportStateManager.cpp
    audio/MidiActivityMonitor.cpp
    audio/ParameterManager.cpp
    # Phase 2 refactoring: Independent features
    audio/PluginWindowBridge.cpp
    audio/WarpMarkerManager.cpp
    # Phase 3 refactoring: Core controllers
    audio/TrackController.cpp
    audio/PluginManager.cpp
    audio/InstrumentRackManager.cpp
    audio/RackSyncManager.cpp
    audio/AutomationPlaybackEngine.cpp
    audio/AutomationRecordingEngine.cpp
    audio/AutomationCurveSimplifier.cpp
    audio/ClipSynchronizer.cpp
    audio/SidechainMonitorPlugin.cpp
    audio/AudioSidechainMonitorPlugin.cpp
    audio/MidiReceivePlugin.cpp
    audio/MagdaSamplerPlugin.cpp
    audio/DrumGridPlugin.cpp
    audio/MidiChordEnginePlugin.cpp
    audio/ArpeggiatorPlugin.cpp
    audio/MidiDevicePlugin.cpp
    audio/StepClock.cpp
    audio/StepSequencerPlugin.cpp
    # Project serialization
    project/serialization/ProjectSerializer.cpp
    project/serialization/TrackSerializer.cpp
    project/serialization/ClipSerializer.cpp
    project/serialization/AutomationModSerializer.cpp
    project/ProjectManager.cpp
    # Preset management
    core/PresetManager.cpp
    # Music theory (chord detection, suggestion, scales)
    music/ChordEngine.cpp
    music/ChordSuggestionEngine.cpp
    # UI components needed by tests
    ui/components/timeline/LoopMarkerInteraction.cpp
    ui/components/timeline/TimelineComponent.cpp
    # State management
    ui/state/TimelineController.cpp
)

# DAW UI sources
set(DAW_UI_SOURCES
    magda_daw_main.cpp
    # Engine (UI layer integration)
    engine/PlaybackPositionTimer.cpp
    # Themes
    ui/themes/DarkTheme.cpp
    ui/themes/FontManager.cpp
    ui/themes/CursorManager.cpp
    ui/themes/MixerLookAndFeel.cpp
    # Windows
    ui/windows/MainWindow.cpp
    ui/windows/MainWindowMenus.cpp
    ui/windows/MainWindowCommands.cpp
    ui/windows/MainWindowExport.cpp
    ui/windows/MenuManager.cpp
    # Dialogs
    ui/dialogs/AISettingsDialog.cpp
    ui/dialogs/AudioSettingsDialog.cpp
    ui/dialogs/ChainTreeDialog.cpp
    ui/dialogs/ExportAudioDialog.cpp
    ui/dialogs/ExportMidiDialog.cpp
    ui/dialogs/ParameterConfigDialog.cpp
    ui/dialogs/PluginSettingsDialog.cpp
    ui/dialogs/PreferencesDialog.cpp
    ui/dialogs/TrackManagerDialog.cpp
    ui/dialogs/AboutDialog.cpp
    ui/dialogs/SplashScreen.cpp
    # Debug
    ui/debug/DebugDialog.cpp
    # Panels
    ui/panels/TransportPanel.cpp
    ui/panels/TimelineHeaderPanel.cpp
    ui/panels/LeftPanel.cpp
    ui/panels/RightPanel.cpp
    ui/panels/BottomPanel.cpp
    ui/panels/FooterBar.cpp
    ui/panels/PanelTabBar.cpp
    ui/panels/TabbedPanel.cpp
    # Panel State
    ui/panels/state/PanelController.cpp
    # Panel Content
    ui/panels/content/PanelContentFactory.cpp
    ui/panels/content/PluginBrowserContent.cpp
    ui/panels/content/MediaExplorerContent.cpp
    ui/panels/content/PresetBrowserContent.cpp
    ui/panels/content/EmptyContent.cpp
    # Inspector refactoring (decomposition into specialized inspectors)
    ui/panels/content/inspector/TrackInspector.cpp
    ui/panels/content/inspector/clip/ClipInspectorInit.cpp
    ui/panels/content/inspector/clip/ClipInspectorSections.cpp
    ui/panels/content/inspector/clip/ClipInspectorLifecycle.cpp
    ui/panels/content/inspector/clip/ClipInspectorLayout.cpp
    ui/panels/content/inspector/clip/ClipInspectorSelection.cpp
    ui/panels/content/inspector/clip/ClipInspectorState.cpp
    ui/panels/content/inspector/NoteInspector.cpp
    ui/panels/content/inspector/DeviceInspector.cpp
    ui/panels/content/inspector/InspectorFactory.cpp
    ui/panels/content/inspector/InspectorContainer.cpp
    ui/panels/content/AIChatConsoleContent.cpp
    ui/panels/content/ScriptingConsoleContent.cpp
    ui/panels/content/DSLTokeniser.cpp
    ui/panels/content/PanelContent.cpp
    ui/panels/content/TrackChainContent.cpp
    ui/panels/content/MidiEditorContent.cpp
    ui/panels/content/PianoRollContent.cpp
    ui/panels/content/DrumGridClipContent.cpp
    ui/panels/content/WaveformEditorContent.cpp
    ui/panels/content/AudioClipPropertiesContent.cpp
    ui/panels/content/ChordPanelContent.cpp
    ui/components/chord/ChordBlockComponent.cpp
    # Audio
    audio/AudioEngineOptimizer.cpp
    # Views
    ui/views/MainView.cpp
    ui/views/SessionView.cpp
    ui/views/SessionClipEditor.cpp
    ui/views/MixerView.cpp
    # Components - Common
    ui/components/common/RampCurveDisplay.cpp
    ui/components/common/TimeBendPopup.cpp
    ui/components/common/SvgButton.cpp
    ui/components/common/IconSelector.cpp
    ui/components/common/ZoomControls.cpp
    ui/components/common/LayoutDebugPanel.cpp
    ui/components/common/MixerDebugPanel.cpp
    ui/components/common/GridOverlayComponent.cpp
    ui/components/common/LinkableTextSlider.cpp
    ui/components/common/DraggableValueLabel.cpp
    ui/components/common/BarsBeatsTicksLabel.cpp
    # Components - Timeline
    ui/components/timeline/LoopMarkerInteraction.cpp
    ui/components/timeline/TimelineComponent.cpp
    ui/components/timeline/TimelineFiller.cpp
    ui/components/timeline/TimeRuler.cpp
    ui/components/timeline/ZoomScrollBar.cpp
    # Components - Tracks
    ui/components/tracks/TrackHeadersPanel.cpp
    ui/components/tracks/TrackContentPanel.cpp
    ui/components/tracks/TrackContentPanelClipDrag.cpp
    # Components - Clips
    ui/components/clips/ClipComponent.cpp
    # Components - Piano Roll
    ui/components/pianoroll/NoteComponent.cpp
    ui/components/pianoroll/PianoRollGridComponent.cpp
    ui/components/pianoroll/PianoRollKeyboard.cpp
    ui/components/pianoroll/VelocityLaneComponent.cpp
    ui/components/pianoroll/CCLaneComponent.cpp
    ui/components/pianoroll/MidiDrawerComponent.cpp
    # Components - Waveform
    ui/components/waveform/WaveformGridComponent.cpp
    # Components - Common Curve Editor
    ui/components/common/curve/CurveEditorBase.cpp
    ui/components/common/curve/CurvePointComponent.cpp
    ui/components/common/curve/CurveTensionHandle.cpp
    ui/components/common/curve/CurveBezierHandle.cpp
    # Components - Automation
    ui/components/automation/AutomationCurveEditor.cpp
    ui/components/automation/AutomationLaneComponent.cpp
    ui/components/automation/AutomationClipComponent.cpp
    # Components - Mixer
    ui/components/mixer/MasterChannelStrip.cpp
    ui/components/mixer/InputTypeSelector.cpp
    ui/components/mixer/RoutingSelector.cpp
    # Components - Chain
    ui/components/chain/NodeComponent.cpp
    ui/components/chain/ChainRowComponent.cpp
    ui/components/chain/RackComponent.cpp
    ui/components/chain/ChainPanel.cpp
    ui/components/chain/DeviceSlotComponent.cpp
    ui/components/chain/ParamGridComponent.cpp
    ui/components/chain/ToneGeneratorUI.cpp
    ui/components/chain/SamplerUI.cpp
    ui/components/chain/DrumGridUI.cpp
    ui/components/chain/EqualiserUI.cpp
    ui/components/chain/CompressorUI.cpp
    ui/components/chain/ReverbUI.cpp
    ui/components/chain/DelayUI.cpp
    ui/components/chain/ChorusUI.cpp
    ui/components/chain/PhaserUI.cpp
    ui/components/chain/FilterUI.cpp
    ui/components/chain/PitchShiftUI.cpp
    ui/components/chain/ImpulseResponseUI.cpp
    ui/components/chain/UtilityUI.cpp
    ui/components/chain/FourOscUI.cpp
    ui/components/chain/ArpeggiatorUI.cpp
    ui/components/chain/StepSequencerUI.cpp
    ui/components/chain/PadChainPanel.cpp
    ui/components/chain/PadDeviceSlot.cpp
    ui/components/chain/PadChainRangeRowComponent.cpp
    ui/components/chain/PadChainRowComponent.cpp
    ui/components/chain/ParamSlotComponent.cpp
    ui/components/chain/ParamLinkResolver.cpp
    ui/components/chain/ParamModulationPainter.cpp
    ui/components/chain/ParamLinkMenu.cpp
    ui/components/chain/ParamWidgetSetup.cpp
    ui/components/chain/PagedControlPanel.cpp
    ui/components/chain/MacroKnobComponent.cpp
    ui/components/chain/MacroPanelComponent.cpp
    ui/components/chain/ModKnobComponent.cpp
    ui/components/chain/ModsPanelComponent.cpp
    ui/components/chain/ModulatorEditorPanel.cpp
    ui/components/chain/MacroEditorPanel.cpp
    ui/components/chain/LFOCurveEditor.cpp
    ui/components/chain/LFOCurveEditorWindow.cpp
    ui/components/chain/LFOPhaseOverlay.cpp
)

# All DAW headers
set(DAW_HEADERS
    command.hpp
    magda.hpp
    # Profiling (header-only)
    profiling/PerformanceProfiler.hpp
    profiling/BenchmarkSuite.hpp
    core/Config.hpp
    core/DeviceInfo.hpp
    core/ViewModeState.hpp
    core/ViewModeEvents.hpp
    core/ViewModeController.hpp
    core/TrackInfo.hpp
    core/TrackManager.hpp
    core/TrackTypes.hpp
    core/RackInfo.hpp
    core/TrackViewSettings.hpp
    core/ClipTypes.hpp
    core/ClipInfo.hpp
    core/ClipManager.hpp
    core/MidiFileWriter.hpp
    core/SelectionManager.hpp
    core/LinkModeManager.hpp
    core/UndoManager.hpp
    core/ClipCommands.hpp
    core/ClipPropertyCommands.hpp
    core/WarpMarkerCommands.hpp
    core/TrackCommands.hpp
    core/TrackPropertyCommands.hpp
    core/AutomationCommands.hpp
    core/MidiNoteCommands.hpp
    core/ChordAnnotationCommands.hpp
    engine/AudioEngine.hpp
    engine/TracktionEngineWrapper.hpp
    engine/MagdaUIBehaviour.hpp
    engine/PlaybackPositionTimer.hpp
    # Interfaces
    interfaces/clip_interface.hpp
    interfaces/mixer_interface.hpp
    interfaces/prompt_interface.hpp
    interfaces/track_interface.hpp
    interfaces/transport_interface.hpp
    # Themes
    ui/themes/DarkTheme.hpp
    ui/themes/FontManager.hpp
    ui/themes/CursorManager.hpp
    ui/themes/MixerLookAndFeel.hpp
    # Windows
    ui/windows/MainWindow.hpp
    ui/windows/MenuManager.hpp
    # Dialogs
    ui/dialogs/AISettingsDialog.hpp
    ui/dialogs/AudioSettingsDialog.hpp
    ui/dialogs/ChainTreeDialog.hpp
    ui/dialogs/ExportAudioDialog.hpp
    ui/dialogs/ExportMidiDialog.hpp
    ui/dialogs/ParameterConfigDialog.hpp
    ui/dialogs/PluginSettingsDialog.hpp
    ui/dialogs/PreferencesDialog.hpp
    ui/dialogs/TrackManagerDialog.hpp
    ui/dialogs/AboutDialog.hpp
    ui/dialogs/SplashScreen.hpp
    # Panels
    ui/panels/TransportPanel.hpp
    ui/panels/TimelineHeaderPanel.hpp
    ui/panels/LeftPanel.hpp
    ui/panels/RightPanel.hpp
    ui/panels/BottomPanel.hpp
    ui/panels/FooterBar.hpp
    ui/panels/PanelTabBar.hpp
    ui/panels/TabbedPanel.hpp
    # Panel State
    ui/panels/state/PanelState.hpp
    ui/panels/state/PanelEvents.hpp
    ui/panels/state/PanelController.hpp
    # Panel Content
    ui/panels/content/PanelContent.hpp
    ui/panels/content/PanelContentFactory.hpp
    ui/panels/content/PluginBrowserContent.hpp
    ui/panels/content/MediaExplorerContent.hpp
    ui/panels/content/PresetBrowserContent.hpp
    ui/panels/content/EmptyContent.hpp
    # Inspector refactoring (decomposition into specialized inspectors)
    ui/panels/content/inspector/BaseInspector.hpp
    ui/panels/content/inspector/TrackInspector.hpp
    ui/panels/content/inspector/ClipInspector.hpp
    ui/panels/content/inspector/NoteInspector.hpp
    ui/panels/content/inspector/DeviceInspector.hpp
    ui/panels/content/inspector/InspectorFactory.hpp
    ui/panels/content/inspector/InspectorContainer.hpp
    ui/panels/content/AIChatConsoleContent.hpp
    ui/panels/content/ScriptingConsoleContent.hpp
    ui/panels/content/DSLTokeniser.hpp
    ui/panels/content/TrackChainContent.hpp
    ui/panels/content/PianoRollContent.hpp
    ui/panels/content/WaveformEditorContent.hpp
    # Audio
    audio/AudioEngineOptimizer.hpp
    audio/AudioBridge.hpp
    audio/SessionClipScheduler.hpp
    audio/SessionClipAudioMonitor.hpp
    audio/SessionClipCommandQueue.hpp
    audio/SessionClipStateQueue.hpp
    audio/SessionMonitorPlugin.hpp
    audio/MeteringBuffer.hpp
    audio/ParameterQueue.hpp
    # Phase 1 refactoring: Pure data managers
    audio/TransportStateManager.hpp
    audio/MidiActivityMonitor.hpp
    audio/ParameterManager.hpp
    # Phase 2 refactoring: Independent features
    audio/PluginWindowBridge.hpp
    audio/WarpMarkerManager.hpp
    # Phase 3 refactoring: Core controllers
    audio/TrackController.hpp
    audio/PluginManager.hpp
    audio/InstrumentRackManager.hpp
    audio/RackSyncManager.hpp
    audio/AutomationPlaybackEngine.hpp
    audio/ClipSynchronizer.hpp
    audio/SidechainTriggerBus.hpp
    audio/SidechainMonitorPlugin.hpp
    audio/AudioSidechainMonitorPlugin.hpp
    audio/MidiBroadcastBus.hpp
    audio/MidiReceivePlugin.hpp
    audio/MagdaSamplerPlugin.hpp
    audio/DrumGridPlugin.hpp
    audio/MidiChordEnginePlugin.hpp
    audio/ArpeggiatorPlugin.hpp
    audio/MidiDevicePlugin.hpp
    audio/StepClock.hpp
    audio/StepSequencerPlugin.hpp
    # Views
    ui/views/MainView.hpp
    ui/views/SessionView.hpp
    ui/views/MixerView.hpp
    ui/views/ArrangementView.hpp
    ui/views/PerformanceView.hpp
    # Layout
    ui/layout/LayoutConfig.hpp
    # State management
    ui/state/TimelineState.hpp
    ui/state/TimelineEvents.hpp
    ui/state/TimelineController.hpp
    # Components - Common
    ui/components/common/SvgButton.hpp
    ui/components/common/IconSelector.hpp
    ui/components/common/ZoomControls.hpp
    ui/components/common/ModeSwitcher.hpp
    ui/components/common/LayoutDebugPanel.hpp
    ui/components/common/MixerDebugPanel.hpp
    ui/components/common/GridOverlayComponent.hpp
    ui/components/common/LinkableTextSlider.hpp
    ui/components/common/DraggableValueLabel.hpp
    ui/components/common/BarsBeatsTicksLabel.hpp
    # Components - Timeline
    ui/components/timeline/LoopMarkerInteraction.hpp
    ui/components/timeline/TimelineComponent.hpp
    ui/components/timeline/TimelineFiller.hpp
    ui/components/timeline/TimeRuler.hpp
    ui/components/timeline/ZoomScrollBar.hpp
    # Components - Tracks
    ui/components/tracks/TrackHeadersPanel.hpp
    ui/components/tracks/TrackContentPanel.hpp
    # Components - Clips
    ui/components/clips/ClipComponent.hpp
    # Components - Piano Roll
    ui/components/pianoroll/NoteComponent.hpp
    ui/components/pianoroll/PianoRollGridComponent.hpp
    ui/components/pianoroll/PianoRollKeyboard.hpp
    ui/components/pianoroll/VelocityLaneComponent.hpp
    ui/components/pianoroll/CCLaneComponent.hpp
    ui/components/pianoroll/MidiDrawerComponent.hpp
    # Components - Waveform
    ui/components/waveform/WaveformGridComponent.hpp
    # Components - Mixer
    ui/components/mixer/MasterChannelStrip.hpp
    ui/components/mixer/InputTypeSelector.hpp
    ui/components/mixer/RoutingSelector.hpp
    ui/components/mixer/MidiNoteStrip.hpp
    # Components - Chain
    ui/components/chain/NodeComponent.hpp
    ui/components/chain/ChainRowComponent.hpp
    ui/components/chain/RackComponent.hpp
    ui/components/chain/ChainPanel.hpp
    ui/components/chain/PadChainPanel.hpp
    ui/components/chain/PadDeviceSlot.hpp
    ui/components/chain/ToneGeneratorUI.hpp
    ui/components/chain/EqualiserUI.hpp
    ui/components/chain/CompressorUI.hpp
    ui/components/chain/ReverbUI.hpp
    ui/components/chain/DelayUI.hpp
    ui/components/chain/ChorusUI.hpp
    ui/components/chain/PhaserUI.hpp
    ui/components/chain/FilterUI.hpp
    ui/components/chain/PitchShiftUI.hpp
    ui/components/chain/ImpulseResponseUI.hpp
    ui/components/chain/UtilityUI.hpp
    ui/components/chain/FourOscUI.hpp
    ui/components/chain/ArpeggiatorUI.hpp
    ui/components/chain/StepSequencerUI.hpp
    ui/components/chain/PagedControlPanel.hpp
    ui/components/chain/MacroKnobComponent.hpp
    ui/components/chain/MacroPanelComponent.hpp
    ui/components/chain/ModKnobComponent.hpp
    ui/components/chain/ModsPanelComponent.hpp
    ui/components/chain/ModulatorEditorPanel.hpp
    ui/components/chain/MacroEditorPanel.hpp
    ui/components/chain/LFOCurveEditor.hpp
    ui/components/chain/ParamLinkResolver.hpp
    ui/components/chain/ParamModulationPainter.hpp
    ui/components/chain/ParamLinkMenu.hpp
    ui/components/chain/ParamWidgetSetup.hpp
    # Core - Macro and Mod
    core/MacroInfo.hpp
    core/ModInfo.hpp
    # Core - Parameter
    core/ParameterInfo.hpp
    core/ParameterDetector.hpp
    core/ParameterUtils.hpp
    # Core - Automation
    core/AutomationTypes.hpp
    core/AutomationInfo.hpp
    core/AutomationManager.hpp
    # Project serialization
    project/ProjectInfo.hpp
    project/serialization/ProjectSerializer.hpp
    project/ProjectManager.hpp
    # Preset management
    core/PresetManager.hpp
    # Components - Common Curve Editor
    ui/components/common/curve/CurveTypes.hpp
    ui/components/common/curve/CurveEditorBase.hpp
    ui/components/common/curve/CurvePointComponent.hpp
    ui/components/common/curve/CurveTensionHandle.hpp
    ui/components/common/curve/CurveBezierHandle.hpp
    # Components - Automation
    ui/components/automation/AutomationCurveEditor.hpp
    ui/components/automation/AutomationLaneComponent.hpp
    ui/components/automation/AutomationClipComponent.hpp
)

# Create DAW library (core functionality)
add_library(magda_daw STATIC ${DAW_CORE_SOURCES})

# Create DAW executable as JUCE application
juce_add_gui_app(magda_daw_app
    VERSION "0.1.0"
    COMPANY_NAME "MAGDA"
    PRODUCT_NAME "MAGDA"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/app_icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/app_icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "MAGDA needs microphone access to record audio."
    PLIST_TO_MERGE
    "<plist version=\"1.0\"><dict>
        <key>NSDocumentsFolderUsageDescription</key>
        <string>MAGDA needs access to your Documents folder to load and save projects.</string>
        <key>NSDesktopFolderUsageDescription</key>
        <string>MAGDA needs access to your Desktop to load and save projects.</string>
        <key>NSDownloadsFolderUsageDescription</key>
        <string>MAGDA needs access to your Downloads folder to import audio files.</string>
        <key>NSRemovableVolumesUsageDescription</key>
        <string>MAGDA needs access to external drives to load and save projects and audio files.</string>
        <key>CFBundleDocumentTypes</key>
        <array>
            <dict>
                <key>CFBundleTypeName</key>
                <string>MAGDA Project</string>
                <key>CFBundleTypeRole</key>
                <string>Editor</string>
                <key>LSHandlerRank</key>
                <string>Owner</string>
                <key>CFBundleTypeExtensions</key>
                <array>
                    <string>mgd</string>
                </array>
            </dict>
        </array>
    </dict></plist>"
)

# Add UI sources to the JUCE app target
target_sources(magda_daw_app PRIVATE ${DAW_UI_SOURCES})

# Link required libraries for DAW core
target_link_libraries(magda_daw
    PUBLIC
    juce::juce_core
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    tracktion::tracktion_engine
    juce_llm
    MagdaAssets
    # Link GTK/WebKit and curl dependencies on Linux
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

# Enable plugin hosting for external VST3/AU plugins
# Enable SoundTouch time stretching for audio clip time/pitch manipulation
# Auto-detect Steinberg ASIO SDK for Windows ASIO support
if(WIN32)
    find_path(ASIO_SDK_DIR
        NAMES "common/iasiodrv.h"
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/asiosdk"
            "${CMAKE_SOURCE_DIR}/third_party/asio_sdk"
            "$ENV{ASIO_SDK_DIR}"
        DOC "Path to Steinberg ASIO SDK"
    )
    if(ASIO_SDK_DIR)
        message(STATUS "ASIO SDK found: ${ASIO_SDK_DIR}")
        set(JUCE_ASIO_ENABLED 1)
    else()
        message(STATUS "ASIO SDK not found - ASIO support disabled. "
            "Download from https://www.steinberg.net/asiosdk and place in third_party/asiosdk/")
        set(JUCE_ASIO_ENABLED 0)
    endif()
endif()

target_compile_definitions(magda_daw
    PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    $<$<BOOL:${JUCE_ASIO_ENABLED}>:JUCE_ASIO=1>
)

# Link required libraries for DAW application
target_link_libraries(magda_daw_app
    PRIVATE
    magda_daw
    magda_agents
    MagdaAssets
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    tracktion::tracktion_engine
    # Link GTK/WebKit and curl dependencies on Linux
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

# Enable plugin hosting for the app target as well
# (needed because JUCE modules are compiled per-target)
target_compile_definitions(magda_daw_app
    PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    $<$<BOOL:${JUCE_ASIO_ENABLED}>:JUCE_ASIO=1>
)

# Add ASIO SDK include paths when available
if(JUCE_ASIO_ENABLED)
    target_include_directories(magda_daw PUBLIC
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
    )
    target_include_directories(magda_daw_app PRIVATE
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
    )
endif()

# Include directories for DAW library
target_include_directories(magda_daw
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/project
    ${CMAKE_CURRENT_SOURCE_DIR}/profiling
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Include directories for DAW application
target_include_directories(magda_daw_app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/themes
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels/state
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels/content
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/views
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/layout
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/state
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/common
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/timeline
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/tracks
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/clips
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/pianoroll
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/waveform
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/chain
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/automation
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/common/curve
    ${CMAKE_SOURCE_DIR}
)

# On MSVC Release builds, generate a PDB for crash symbolization.
# /Zi is applied here (not globally) so llama.cpp and other third-party targets
# are not affected and don't hit parallel PDB write conflicts when using sccache.
if(MSVC)
    target_compile_options(magda_daw_app PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(magda_daw_app PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

# Install targets
install(TARGETS magda_daw_app DESTINATION bin)
install(FILES ${DAW_HEADERS} DESTINATION include/magda/daw)

# =============================================================================
# Plugin Scanner Executable (for out-of-process plugin scanning)
# =============================================================================

# Create the plugin scanner as a console app
juce_add_console_app(magda_plugin_scanner
    VERSION "1.0.0"
    COMPANY_NAME "MAGDA"
    PRODUCT_NAME "MAGDA Plugin Scanner"
)

target_sources(magda_plugin_scanner PRIVATE
    engine/plugin_scanner_main.cpp
)

target_link_libraries(magda_plugin_scanner
    PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
    # Link curl dependencies on Linux for networking
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

target_compile_definitions(magda_plugin_scanner
    PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
)

# Link AudioUnit framework for AU plugin hosting on macOS
if(APPLE)
    target_link_libraries(magda_plugin_scanner
        PRIVATE
        "-framework AudioUnit"
        "-framework CoreAudioKit"
    )
endif()

target_include_directories(magda_plugin_scanner
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Copy scanner executable alongside the main app
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/MacOS/magda_plugin_scanner"
        COMMENT "Copying plugin scanner to app bundle"
    )
elseif(WIN32)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_FILE_DIR:magda_daw_app>/magda_plugin_scanner.exe"
        COMMENT "Copying plugin scanner alongside app executable"
    )
else()
    # Linux: copy scanner next to the main executable
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_FILE_DIR:magda_daw_app>/magda_plugin_scanner"
        COMMENT "Copying plugin scanner alongside app executable"
    )
endif()

# Copy lang/ (localization JSON) into the app so StringTable can load it at runtime.
set(MAGDA_LANG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../lang")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_LANG_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/lang"
        COMMENT "Copying localization files to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_LANG_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/lang"
        COMMENT "Copying localization files alongside app executable"
    )
endif()
