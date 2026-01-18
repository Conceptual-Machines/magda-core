# DAW Domain - Digital Audio Workstation Core + UI
# Simple version focusing on Tracktion Engine integration

# Create binary data for fonts and icons
juce_add_binary_data(MagicaAssets
    SOURCES
    # Fonts
    ../assets/fonts/Inter-Regular.ttf
    ../assets/fonts/Inter-Medium.ttf
    ../assets/fonts/Inter-SemiBold.ttf
    ../assets/fonts/Inter-Bold.ttf
    # Transport icons (off/on states)
    ../assets/icons/play_off.svg
    ../assets/icons/play_on.svg
    ../assets/icons/pause_off.svg
    ../assets/icons/pause_on.svg
    ../assets/icons/stop_off.svg
    ../assets/icons/stop_on.svg
    ../assets/icons/record_off.svg
    ../assets/icons/record_on.svg
    ../assets/icons/loop_off.svg
    ../assets/icons/loop_on.svg
    ../assets/icons/forward_off.svg
    ../assets/icons/forward_on.svg
    ../assets/icons/rewind_off.svg
    ../assets/icons/rewind_on.svg
    ../assets/icons/prev_off.svg
    ../assets/icons/prev_on.svg
    ../assets/icons/next_off.svg
    ../assets/icons/next_on.svg
    # Audio icons
    ../assets/icons/volume_up.svg
    ../assets/icons/volume_down.svg
    ../assets/icons/metronome.svg
    # UI icons
    ../assets/icons/lock.svg
    ../assets/icons/lock_open.svg
    ../assets/icons/add.svg
    ../assets/icons/remove.svg
    ../assets/icons/settings.svg
    ../assets/icons/menu.svg
    ../assets/icons/close.svg
    ../assets/icons/check.svg
    ../assets/icons/search.svg
    ../assets/icons/zoom_in.svg
    ../assets/icons/zoom_out.svg
    # Mouse cursors
    ../assets/icons/mouse_zoom/search.svg
    ../assets/icons/mouse_zoom/zoom_in.svg
    ../assets/icons/mouse_zoom/zoom_out.svg
    # Footer view mode icons
    ../assets/icons/footer/Session.svg
    ../assets/icons/footer/Arrangement.svg
    ../assets/icons/footer/Mix.svg
    ../assets/icons/footer/Master.svg
)

# DAW Core sources
set(DAW_CORE_SOURCES
    command.cpp
    magica.cpp
    core/Config.cpp
    core/ViewModeController.cpp
    core/TrackManager.cpp
    engine/tracktion_engine_wrapper.cpp
    # UI components needed by tests
    ui/components/timeline/TimelineComponent.cpp
    ui/components/timeline/ZoomManager.cpp
    # State management
    ui/state/TimelineController.cpp
)

# DAW UI sources
set(DAW_UI_SOURCES
    magica_daw_main.cpp
    # Themes
    ui/themes/DarkTheme.cpp
    ui/themes/FontManager.cpp
    ui/themes/CursorManager.cpp
    # Windows
    ui/windows/MainWindow.cpp
    ui/windows/MenuManager.cpp
    # Dialogs
    ui/dialogs/PreferencesDialog.cpp
    # Panels
    ui/panels/TransportPanel.cpp
    ui/panels/TimelineHeaderPanel.cpp
    ui/panels/LeftPanel.cpp
    ui/panels/RightPanel.cpp
    ui/panels/BottomPanel.cpp
    ui/panels/FooterBar.cpp
    # Audio
    audio/AudioEngineOptimizer.cpp
    # Views
    ui/views/MainView.cpp
    ui/views/SessionView.cpp
    ui/views/MixerView.cpp
    # Components - Common
    ui/components/common/SvgButton.cpp
    ui/components/common/ZoomControls.cpp
    ui/components/common/LayoutDebugPanel.cpp
    ui/components/common/GridOverlayComponent.cpp
    # Components - Timeline
    ui/components/timeline/TimelineComponent.cpp
    ui/components/timeline/TimelineFiller.cpp
    ui/components/timeline/TimeRuler.cpp
    ui/components/timeline/ZoomManager.cpp
    ui/components/timeline/ZoomScrollBar.cpp
    # Components - Tracks
    ui/components/tracks/TrackHeadersPanel.cpp
    ui/components/tracks/TrackContentPanel.cpp
    # Components - Mixer
    ui/components/mixer/MasterChannelStrip.cpp
)

# All DAW headers
set(DAW_HEADERS
    command.hpp
    magica.hpp
    core/Config.hpp
    core/ViewModeState.hpp
    core/ViewModeEvents.hpp
    core/ViewModeController.hpp
    core/TrackInfo.hpp
    core/TrackManager.hpp
    engine/tracktion_engine_wrapper.hpp
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
    # Windows
    ui/windows/MainWindow.hpp
    ui/windows/MenuManager.hpp
    # Dialogs
    ui/dialogs/PreferencesDialog.hpp
    # Panels
    ui/panels/TransportPanel.hpp
    ui/panels/TimelineHeaderPanel.hpp
    ui/panels/LeftPanel.hpp
    ui/panels/RightPanel.hpp
    ui/panels/BottomPanel.hpp
    ui/panels/FooterBar.hpp
    # Audio
    audio/AudioEngineOptimizer.hpp
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
    ui/components/common/GridOverlayComponent.hpp
    # Components - Timeline
    ui/components/timeline/TimelineComponent.hpp
    ui/components/timeline/TimelineFiller.hpp
    ui/components/timeline/TimeRuler.hpp
    ui/components/timeline/ZoomManager.hpp
    ui/components/timeline/ZoomScrollBar.hpp
    # Components - Tracks
    ui/components/tracks/TrackHeadersPanel.hpp
    ui/components/tracks/TrackContentPanel.hpp
    # Components - Mixer
    ui/components/mixer/MasterChannelStrip.hpp
)

# Create DAW library (core functionality)
add_library(magica_daw STATIC ${DAW_CORE_SOURCES})

# Create DAW executable as JUCE application
juce_add_gui_app(magica_daw_app
    VERSION "0.1.0"
    COMPANY_NAME "Magica"
    PRODUCT_NAME "Magica DAW"
)

# Add UI sources to the JUCE app target
target_sources(magica_daw_app PRIVATE ${DAW_UI_SOURCES})

# Link required libraries for DAW core
target_link_libraries(magica_daw
    PUBLIC
    juce::juce_core
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_gui_basics
    tracktion::tracktion_engine
    MagicaAssets
)

# Link required libraries for DAW application
target_link_libraries(magica_daw_app
    PRIVATE
    magica_daw
    MagicaAssets
    juce::juce_gui_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    tracktion::tracktion_engine
)

# Include directories for DAW library
target_include_directories(magica_daw
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Include directories for DAW application
target_include_directories(magica_daw_app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/themes
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/views
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/layout
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/state
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/common
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/timeline
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/tracks
    ${CMAKE_SOURCE_DIR}
)

# Install targets
install(TARGETS magica_daw_app DESTINATION bin)
install(FILES ${DAW_HEADERS} DESTINATION include/magica/daw)
