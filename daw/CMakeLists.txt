# DAW Domain - Digital Audio Workstation Core + UI
# Simple version focusing on Tracktion Engine integration

# DAW Core sources
set(DAW_CORE_SOURCES
    command.cpp
    magica.cpp
    engine/tracktion_engine_wrapper.cpp
)

# DAW UI sources
set(DAW_UI_SOURCES
    magica_daw_main.cpp
)

# All DAW headers
set(DAW_HEADERS
    command.hpp
    magica.hpp
    engine/tracktion_engine_wrapper.hpp
    interfaces/clip_interface.hpp
    interfaces/mixer_interface.hpp
    interfaces/prompt_interface.hpp
    interfaces/track_interface.hpp
    interfaces/transport_interface.hpp
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
    tracktion::tracktion_engine
)

# Link required libraries for DAW application
target_link_libraries(magica_daw_app
    PRIVATE
    magica_daw
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
    ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Include directories for DAW application
target_include_directories(magica_daw_app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Install targets
install(TARGETS magica_daw_app DESTINATION bin)
install(FILES ${DAW_HEADERS} DESTINATION include/magica/daw) 