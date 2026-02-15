# Test sources
set(TEST_SOURCES
    test_audio_bridge.cpp
    test_audiobridge_track_limit_bug.cpp
    test_audiobridge_plugin_cleanup_bug.cpp
    test_auto_tempo.cpp
    test_audio_clip_stretch.cpp
    test_audio_source_length.cpp
    test_audio_export.cpp
    test_command.cpp
    test_interfaces.cpp
    test_midi_clip_sync.cpp
    test_midi_clip_split.cpp
    test_midi_offset.cpp
    test_nested_racks.cpp
    test_rack_audio.cpp
    test_modulation.cpp
    test_group_tracks_and_macros.cpp
    test_parameter_utils.cpp
    test_plugin_loading.cpp
    test_plugin_format.cpp
    test_plugin_window_manager.cpp
    test_device_parameter_pagination.cpp
    test_waveform_editor_absolute_mode.cpp
    test_clip_commands.cpp
    test_clip_resize_operations.cpp
    test_looped_waveform_tile.cpp
    test_project_serialization.cpp
    test_session_clip_playback.cpp
    test_sidechain_triggers.cpp
    test_curve_snapshot.cpp
    test_scan_report.cpp
    test_drum_grid_plugin.cpp
    test_velocity_lane_utils.cpp
    test_grid_constants.cpp
    test_multi_out_tracks.cpp
)

# Create test executable
add_executable(magda_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(magda_tests
    PRIVATE
    magda_daw
    Catch2::Catch2WithMain
    juce::juce_core
    juce::juce_gui_basics
)

# Include directories
target_include_directories(magda_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Add the test to CTest
add_test(NAME magda_unit_tests COMMAND magda_tests)

# For Catch2 test discovery (optional - automatically discovers individual test cases)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include(Catch)
    catch_discover_tests(magda_tests)
endif()

# JUCE Unit Test Console Application
juce_add_console_app(magda_juce_tests
    VERSION "0.1.0"
    COMPANY_NAME "MAGDA"
    PRODUCT_NAME "Magica JUCE Tests"
)

# Add JUCE test sources
target_sources(magda_juce_tests PRIVATE
    juce_tests_main.cpp
    test_zoom_simple.cpp
    test_export_transport_stop_juce.cpp
    test_tracktion_engine_wrapper_refactoring_juce.cpp
    # test_export_no_assertion_juce.cpp  # Disabled - see issue #611 (generator device export)
    test_midi_recording_juce.cpp
    test_clip_sync_integration_juce.cpp
)

# Link required libraries for JUCE tests
target_link_libraries(magda_juce_tests
    PRIVATE
    magda_daw
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    # Link curl dependencies on Linux
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

# Include directories for JUCE tests
target_include_directories(magda_juce_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)
