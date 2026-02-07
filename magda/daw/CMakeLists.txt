# DAW Domain - Digital Audio Workstation Core + UI
# Simple version focusing on Tracktion Engine integration

# Create binary data for fonts and icons
juce_add_binary_data(MagdaAssets
    SOURCES
    # Fonts
    ../../assets/fonts/Inter-Regular.ttf
    ../../assets/fonts/Inter-Medium.ttf
    ../../assets/fonts/Inter-SemiBold.ttf
    ../../assets/fonts/Inter-Bold.ttf
    # Transport icons (off/on states)
    ../../assets/icons/play_off.svg
    ../../assets/icons/play_on.svg
    ../../assets/icons/pause_off.svg
    ../../assets/icons/pause_on.svg
    ../../assets/icons/stop_off.svg
    ../../assets/icons/stop_on.svg
    ../../assets/icons/record_off.svg
    ../../assets/icons/record_on.svg
    ../../assets/icons/loop_off.svg
    ../../assets/icons/loop_on.svg
    ../../assets/icons/forward_off.svg
    ../../assets/icons/forward_on.svg
    ../../assets/icons/rewind_off.svg
    ../../assets/icons/rewind_on.svg
    ../../assets/icons/prev_off.svg
    ../../assets/icons/prev_on.svg
    ../../assets/icons/next_off.svg
    ../../assets/icons/next_on.svg
    # Audio icons
    ../../assets/icons/volume_up.svg
    ../../assets/icons/volume_down.svg
    ../../assets/icons/volume_off.svg
    ../../assets/icons/metronome.svg
    # UI icons
    ../../assets/icons/lock.svg
    ../../assets/icons/lock_open.svg
    ../../assets/icons/add.svg
    ../../assets/icons/remove.svg
    ../../assets/icons/settings.svg
    ../../assets/icons/settings_nobg.svg
    ../../assets/icons/menu.svg
    ../../assets/icons/close.svg
    ../../assets/icons/check.svg
    ../../assets/icons/search.svg
    ../../assets/icons/Chords2.svg
    ../../assets/icons/link.svg
    ../../assets/icons/link_bright.svg
    ../../assets/icons/link_flat.svg
    ../../assets/icons/knob.svg
    ../../assets/icons/sine-wave.svg
    ../../assets/icons/sine-wave-bright.svg
    ../../assets/icons/bare_sine.svg
    ../../assets/icons/open_in_new.svg
    ../../assets/icons/power_on.svg
    ../../assets/icons/rack.svg
    ../../assets/icons/tree.svg
    ../../assets/icons/bezier.svg
    ../../assets/icons/curve.svg
    ../../assets/icons/save.svg
    ../../assets/icons/infinito.svg
    ../../assets/icons/position.svg
    ../../assets/icons/Offset.svg
    # Footer view mode icons
    ../../assets/icons/footer/Session.svg
    ../../assets/icons/footer/Arrangement.svg
    ../../assets/icons/footer/Mix.svg
    ../../assets/icons/footer/Master.svg
    # Panel content type icons
    ../../assets/icons/sidebar/plug.svg
    ../../assets/icons/sidebar/sample.svg
    ../../assets/icons/sidebar/preset.svg
    ../../assets/icons/sidebar/info.svg
    ../../assets/icons/sidebar/console.svg
    ../../assets/icons/script.svg
    ../../assets/icons/browser.svg
    # Media Explorer sidebar icons
    ../../assets/icons/project_home.svg
    ../../assets/icons/harddrive.svg
    ../../assets/icons/library.svg
    # Media Explorer filter icons
    ../../assets/midi.svg
    # Mixer fader icons
    ../../assets/icons/fader/fader_thumb.svg
    ../../assets/icons/fader/fader_track.svg
    # Mixer knob icons
    ../../assets/icons/knob/knob_body.svg
    ../../assets/icons/knob/knob_pointer.svg
    # View toolbar icons
    ../../assets/icons/zoom_out_map.svg
    ../../assets/icons/fit_width.svg
    ../../assets/icons/compact.svg
    ../../assets/icons/large.svg
    # I/O routing icons
    ../../assets/icons/IO/Input.svg
    ../../assets/icons/IO/Output.svg
    # Inspector clip icons
    ../../assets/icons/pin.svg
    ../../assets/icons/note.svg
    # Fade curve type icons
    ../../assets/icons/fade_linear.svg
    ../../assets/icons/fade_convex.svg
    ../../assets/icons/fade_concave.svg
    ../../assets/icons/fade_scurve.svg
    # Fade behaviour icons
    ../../assets/icons/fade_gain.svg
    ../../assets/icons/fade_speedramp.svg
    # Punch in/out icons
    ../../assets/icons/punchinout/punchin.svg
    ../../assets/icons/punchinout/punchin_on.svg
    ../../assets/icons/punchinout/punchout.svg
    ../../assets/icons/punchinout/punchout_on.svg
)

# DAW Core sources
set(DAW_CORE_SOURCES
    command.cpp
    magda.cpp
    core/Config.cpp
    core/ViewModeController.cpp
    core/TrackManager.cpp
    core/ModulatorEngine.cpp
    core/ClipManager.cpp
    core/SelectionManager.cpp
    core/AutomationManager.cpp
    core/LinkModeManager.cpp
    core/UndoManager.cpp
    core/ClipCommands.cpp
    core/TrackCommands.cpp
    core/MidiNoteCommands.cpp
    core/ParameterUtils.cpp
    engine/TracktionEngineWrapper.cpp
    engine/MagdaUIBehaviour.cpp
    engine/PluginScanner.cpp
    engine/PluginScanCoordinator.cpp
    engine/PluginWindowManager.cpp
    # Audio integration
    audio/AudioBridge.cpp
    audio/AudioThumbnailManager.cpp
    audio/DeviceProcessor.cpp
    audio/MidiBridge.cpp
    audio/SessionClipScheduler.cpp
    # Phase 1 refactoring: Pure data managers
    audio/TransportStateManager.cpp
    audio/MidiActivityMonitor.cpp
    audio/ParameterManager.cpp
    # Phase 2 refactoring: Independent features
    audio/PluginWindowBridge.cpp
    audio/WarpMarkerManager.cpp
    # TODO: Custom synth library (SimpleSynthPlugin.cpp) - for future implementation
    # Project serialization
    project/ProjectSerializer.cpp
    project/ProjectManager.cpp
    # Preset management
    core/PresetManager.cpp
    # UI components needed by tests
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
    ui/windows/MenuManager.cpp
    # Dialogs
    ui/dialogs/AudioSettingsDialog.cpp
    ui/dialogs/ChainTreeDialog.cpp
    ui/dialogs/ExportAudioDialog.cpp
    ui/dialogs/ParameterConfigDialog.cpp
    ui/dialogs/PreferencesDialog.cpp
    ui/dialogs/TrackManagerDialog.cpp
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
    ui/panels/content/InspectorContent.cpp
    ui/panels/content/AIChatConsoleContent.cpp
    ui/panels/content/ScriptingConsoleContent.cpp
    ui/panels/content/TrackChainContent.cpp
    ui/panels/content/PianoRollContent.cpp
    ui/panels/content/WaveformEditorContent.cpp
    # Audio
    audio/AudioEngineOptimizer.cpp
    # Views
    ui/views/MainView.cpp
    ui/views/SessionView.cpp
    ui/views/SessionClipEditor.cpp
    ui/views/MixerView.cpp
    # Components - Common
    ui/components/common/SvgButton.cpp
    ui/components/common/ZoomControls.cpp
    ui/components/common/LayoutDebugPanel.cpp
    ui/components/common/MixerDebugPanel.cpp
    ui/components/common/GridOverlayComponent.cpp
    ui/components/common/DraggableValueLabel.cpp
    ui/components/common/BarsBeatsTicksLabel.cpp
    # Components - Timeline
    ui/components/timeline/TimelineComponent.cpp
    ui/components/timeline/TimelineFiller.cpp
    ui/components/timeline/TimeRuler.cpp
    ui/components/timeline/ZoomScrollBar.cpp
    # Components - Tracks
    ui/components/tracks/TrackHeadersPanel.cpp
    ui/components/tracks/TrackContentPanel.cpp
    # Components - Clips
    ui/components/clips/ClipComponent.cpp
    # Components - Piano Roll
    ui/components/pianoroll/NoteComponent.cpp
    ui/components/pianoroll/PianoRollGridComponent.cpp
    ui/components/pianoroll/PianoRollKeyboard.cpp
    ui/components/pianoroll/VelocityLaneComponent.cpp
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
    ui/components/mixer/RoutingSelector.cpp
    # Components - Chain
    ui/components/chain/NodeComponent.cpp
    ui/components/chain/ChainRowComponent.cpp
    ui/components/chain/RackComponent.cpp
    ui/components/chain/ChainPanel.cpp
    ui/components/chain/DeviceSlotComponent.cpp
    ui/components/chain/ToneGeneratorUI.cpp
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
    core/SelectionManager.hpp
    core/LinkModeManager.hpp
    core/UndoManager.hpp
    core/ClipCommands.hpp
    core/TrackCommands.hpp
    core/MidiNoteCommands.hpp
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
    ui/dialogs/AudioSettingsDialog.hpp
    ui/dialogs/ChainTreeDialog.hpp
    ui/dialogs/ExportAudioDialog.hpp
    ui/dialogs/ParameterConfigDialog.hpp
    ui/dialogs/PreferencesDialog.hpp
    ui/dialogs/TrackManagerDialog.hpp
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
    ui/panels/content/InspectorContent.hpp
    ui/panels/content/AIChatConsoleContent.hpp
    ui/panels/content/ScriptingConsoleContent.hpp
    ui/panels/content/TrackChainContent.hpp
    ui/panels/content/PianoRollContent.hpp
    ui/panels/content/WaveformEditorContent.hpp
    # Audio
    audio/AudioEngineOptimizer.hpp
    audio/AudioBridge.hpp
    audio/SessionClipScheduler.hpp
    audio/MeteringBuffer.hpp
    audio/ParameterQueue.hpp
    # Phase 1 refactoring: Pure data managers
    audio/TransportStateManager.hpp
    audio/MidiActivityMonitor.hpp
    audio/ParameterManager.hpp
    # Phase 2 refactoring: Independent features
    audio/PluginWindowBridge.hpp
    audio/WarpMarkerManager.hpp
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
    ui/components/common/ZoomControls.hpp
    ui/components/common/ModeSwitcher.hpp
    ui/components/common/LayoutDebugPanel.hpp
    ui/components/common/MixerDebugPanel.hpp
    ui/components/common/GridOverlayComponent.hpp
    ui/components/common/DraggableValueLabel.hpp
    ui/components/common/BarsBeatsTicksLabel.hpp
    # Components - Timeline
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
    # Components - Waveform
    ui/components/waveform/WaveformGridComponent.hpp
    # Components - Mixer
    ui/components/mixer/MasterChannelStrip.hpp
    ui/components/mixer/RoutingSelector.hpp
    # Components - Chain
    ui/components/chain/NodeComponent.hpp
    ui/components/chain/ChainRowComponent.hpp
    ui/components/chain/RackComponent.hpp
    ui/components/chain/ChainPanel.hpp
    ui/components/chain/ToneGeneratorUI.hpp
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
    core/ParameterUtils.hpp
    # Core - Automation
    core/AutomationTypes.hpp
    core/AutomationInfo.hpp
    core/AutomationManager.hpp
    # Project serialization
    project/ProjectInfo.hpp
    project/ProjectSerializer.hpp
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
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/Sleek-M.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/Sleek-M.png"
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
    tracktion::tracktion_engine
    MagdaAssets
    # Link GTK/WebKit and curl dependencies on Linux
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

# Enable plugin hosting for external VST3/AU plugins
# Enable SoundTouch time stretching for audio clip time/pitch manipulation
target_compile_definitions(magda_daw
    PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
)

# Link required libraries for DAW application
target_link_libraries(magda_daw_app
    PRIVATE
    magda_daw
    MagdaAssets
    juce::juce_gui_basics
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
)

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
)

# Copy scanner to app bundle on Mac
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/MacOS/magda_plugin_scanner"
        COMMENT "Copying plugin scanner to app bundle"
    )
endif()
