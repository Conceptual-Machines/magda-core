# DAW Domain - Digital Audio Workstation Core + UI
# Simple version focusing on Tracktion Engine integration

# Create binary data for fonts and icons
juce_add_binary_data(MagicaAssets
    SOURCES
    ../assets/fonts/Inter-Regular.ttf
    ../assets/fonts/Inter-Medium.ttf
    ../assets/fonts/Inter-SemiBold.ttf
    ../assets/fonts/Inter-Bold.ttf
    ../assets/icons/play.svg
    ../assets/icons/pause.svg
    ../assets/icons/stop.svg
    ../assets/icons/record.svg
    ../assets/icons/loop.svg
    ../assets/icons/repeat.svg
    ../assets/icons/repeat_one.svg
    ../assets/icons/volume_up.svg
    ../assets/icons/lock.svg
    ../assets/icons/lock_open.svg
)

# DAW Core sources
set(DAW_CORE_SOURCES
    command.cpp
    magica.cpp
    core/Config.cpp
    engine/tracktion_engine_wrapper.cpp
    # UI components needed by tests
    ui/components/TimelineComponent.cpp
    ui/components/ZoomManager.cpp
)

# DAW UI sources
set(DAW_UI_SOURCES
    magica_daw_main.cpp
    ui/themes/DarkTheme.cpp
    ui/themes/FontManager.cpp
    ui/windows/MainWindow.cpp
    ui/windows/MenuManager.cpp
    ui/panels/TransportPanel.cpp
    ui/panels/TimelineHeaderPanel.cpp
    ui/panels/LeftPanel.cpp
    ui/panels/RightPanel.cpp
    ui/panels/BottomPanel.cpp
    ui/views/MainView.cpp
    ui/components/TimelineComponent.cpp
    ui/components/TrackHeadersPanel.cpp
    ui/components/TrackContentPanel.cpp
    ui/components/SvgButton.cpp
    ui/components/TimelineFiller.cpp
    ui/components/ZoomManager.cpp
)

# All DAW headers
set(DAW_HEADERS
    command.hpp
    magica.hpp
    core/Config.hpp
    engine/tracktion_engine_wrapper.hpp
    interfaces/clip_interface.hpp
    interfaces/mixer_interface.hpp
    interfaces/prompt_interface.hpp
    interfaces/track_interface.hpp
    interfaces/transport_interface.hpp
    ui/themes/DarkTheme.hpp
    ui/themes/FontManager.hpp
    ui/windows/MainWindow.hpp
    ui/windows/MenuManager.hpp
    ui/panels/TransportPanel.hpp
    ui/panels/TimelineHeaderPanel.hpp
    ui/panels/LeftPanel.hpp
    ui/panels/RightPanel.hpp
    ui/panels/BottomPanel.hpp
    ui/views/MainView.hpp
    ui/components/TimelineComponent.hpp
    ui/components/TrackHeadersPanel.hpp
    ui/components/TrackContentPanel.hpp
    ui/components/SvgButton.hpp
    ui/components/ZoomManager.hpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/themes
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/views
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components
    ${CMAKE_SOURCE_DIR}
)

# Install targets
install(TARGETS magica_daw_app DESTINATION bin)
install(FILES ${DAW_HEADERS} DESTINATION include/magica/daw) 