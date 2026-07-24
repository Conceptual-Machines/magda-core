# Music-theory leaf: chord detection, notation, and suggestion engines.
# JUCE modules are compiled once by magda_daw; borrow their compile usage here
# without linking their INTERFACE_SOURCES into a second static archive.
add_library(magda_music STATIC
    ChordEngine.cpp
    ChordSuggestionEngine.cpp
    ChordCache.hpp
    ChordEngine.hpp
    ChordEnums.hpp
    ChordSuggestionEngine.hpp
    ChordTypes.hpp
    KeyModeHistogram.hpp
    LRUCache.hpp
    NotationSettings.hpp
    ScaleDetector.hpp
    Scales.hpp
)

target_compile_features(magda_music PUBLIC cxx_std_20)
target_include_directories(magda_music
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    $<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:juce::juce_events,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(magda_music PUBLIC
    $<TARGET_PROPERTY:juce::juce_core,INTERFACE_COMPILE_DEFINITIONS>
    $<TARGET_PROPERTY:juce::juce_events,INTERFACE_COMPILE_DEFINITIONS>
)

# A leaf music source must not reach upward into DAW, device, engine, project,
# UI, or other application layers.
get_target_property(_magda_music_sources magda_music SOURCES)
foreach(_magda_music_source IN LISTS _magda_music_sources)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_magda_music_source}" _magda_music_source_text)
    if(_magda_music_source_text
       MATCHES "(^|\n)[ \t]*#[ \t]*include[ \t]*[<\"](\\.\\./|audio/|core/|engine/|project/|ui/|agents/)")
        message(FATAL_ERROR
            "magda_music source crosses its leaf-library boundary: ${_magda_music_source}")
    endif()
endforeach()
