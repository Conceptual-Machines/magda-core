# Test sources
set(TEST_SOURCES
    test_audio_bridge.cpp
    test_audiobridge_track_limit_bug.cpp
    test_audiobridge_plugin_cleanup_bug.cpp
    test_auto_tempo.cpp
    test_audio_clip_stretch.cpp
    test_clip_bpm_invariant.cpp
    test_audio_source_length.cpp
    test_audio_export.cpp
    test_command.cpp
    test_interfaces.cpp
    test_midi_clip_sync.cpp
    test_midi_clip_split.cpp
    test_midi_offset.cpp
    test_pianoroll_note_position.cpp
    test_nested_racks.cpp
    test_rack_audio.cpp
    test_modulation.cpp
    test_control_target.cpp
    test_chain_fingerprint.cpp
    test_chain_routing_model.cpp
    test_group_tracks_and_macros.cpp
    test_parameter_utils.cpp
    test_update_checker.cpp
    test_plugin_loading.cpp
    test_plugin_format.cpp
    test_plugin_window_manager.cpp
    test_prune_missing_plugins.cpp
    test_known_plugin_list_duplicates.cpp
    test_device_parameter_pagination.cpp
    test_waveform_editor_absolute_mode.cpp
    test_clip_batch_edit.cpp
    test_clip_commands.cpp
    test_clip_resize_operations.cpp
    test_midi_clip_resize_left.cpp
    test_looped_waveform_tile.cpp
    test_project_serialization.cpp
    test_session_clip_playback.cpp
    test_sidechain_triggers.cpp
    test_curve_snapshot.cpp
    test_automation_curve_simplifier.cpp
    test_scan_report.cpp
    test_drum_grid_plugin.cpp
    test_velocity_lane_utils.cpp
    test_grid_constants.cpp
    test_ui_scale.cpp
    test_timeline_loop_activation.cpp
    test_multi_out_tracks.cpp
    test_freeze_track.cpp
    test_dsl_add_chord.cpp
    test_dsl_add_arpeggio.cpp
    test_scale_detection.cpp
    test_step_clock.cpp
    test_mono_note_processor.cpp
    test_sends_and_track_deletion.cpp
    test_master_track_serialization.cpp
    test_automation_agent.cpp
    test_automation_modes.cpp
    test_automation_singleton.cpp
    test_instruction_executor_context.cpp
    # Faust pool refactor (Phase 1: metadata parser)
    test_faust_metadata_parser.cpp
    test_faust_param_pool.cpp
    test_faust_param_info.cpp
    # Parameter alias system tests (PR 1)
    test_param_name_normalize.cpp
    test_alias_parser.cpp
    # Parameter alias system tests (PR 2)
    test_alias_registry.cpp
    # Parameter alias system tests (PR 3)
    test_alias_resolver.cpp
    # Parameter alias system tests (PR 4 -- AutoAliasGenerator)
    test_alias_auto_gen.cpp
    # Parameter alias system tests (PR 5 -- CuratedAliasLoader)
    test_curated_alias_loader.cpp
    # Parameter alias system tests (PR 7 -- Automation alias targets)
    test_automation_alias_targets.cpp
    # Controller data model tests (issue #1050 -- Phase A)
    test_binding_transform.cpp
    # Controller registry tests (issue #1050 -- Phase B)
    test_controller_registry.cpp
    test_binding_registry.cpp
    # Controller router tests (issue #1050 -- Phase C)
    test_controller_router.cpp
    # MIDI device identifier matcher tests (issue #1050)
    test_midi_device_match.cpp
    # MIDI Learn session tests (issue #924)
    test_midi_learn.cpp
    # Controller profile tests (issue #1052)
    test_controller_profile.cpp
    # Lua runtime tests (issue #29)
    test_lua_runtime.cpp
    # Lua bindings tests (issue #30 — bind MagdaApi)
    test_magda_api_lua_bindings.cpp
    # Lua controller tests (issue #592 — MIDI dispatch)
    test_lua_controller.cpp
    # Focused device API tests
    test_focused_api_live.cpp
    # SelectionManager listener reentrancy
    test_selection_manager_listeners.cpp
    # Configurable user data paths
    test_app_paths.cpp
)

# Create test executable
add_executable(magda_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(magda_tests
    PRIVATE
    magda_daw
    magda_agents
    magda_scripting
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
    test_session_slot_recording_juce.cpp
    test_timeline_loop_activation_juce.cpp
    test_midi_signal_routing_juce.cpp
    test_clip_inspector_juce.cpp
    test_multiband_curve_view_juce.cpp
    ../magda/daw/ui/components/chain/compiled/CompiledMultibandCurveView.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorInit.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorSections.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorLifecycle.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorLayout.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorSelection.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorState.cpp
    ../magda/daw/ui/panels/content/inspector/clip/sections/ClipFadesSection.cpp
    ../magda/daw/ui/components/common/BarsBeatsTicksLabel.cpp
    ../magda/daw/ui/components/common/ValueLabelControl.cpp
    ../magda/daw/ui/components/common/DraggableValueLabel.cpp
    ../magda/daw/ui/components/common/SvgButton.cpp
    ../magda/daw/ui/themes/FontManager.cpp
    test_automation_modes_juce.cpp
    test_arrangement_transport_loop_juce.cpp
)

# Link required libraries for JUCE tests
target_link_libraries(magda_juce_tests
    PRIVATE
    magda_daw
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    # Linux: juce_gui_extra is pulled in transitively via magda_daw's
    # JUCE_MODULE_AVAILABLE flags (so its .cpp gets compiled into this
    # target). Its compilation requires GTK / WebKit headers — expose
    # the same pkgconfig deps the GUI app uses, plus curl.
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

target_compile_definitions(magda_juce_tests PRIVATE MAGDA_ENABLE_TEST_HOOKS=1)

# Include directories for JUCE tests
target_include_directories(magda_juce_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/magda/daw/ui
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/themes
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/panels
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/panels/state
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/panels/content
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/state
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/components
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/components/chain
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/components/common
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/components/common/curve
)

add_test(NAME arrangement_transport_loop_juce
         COMMAND magda_juce_tests "Arrangement Transport Loop Tests")

add_test(NAME multiband_curve_view_juce
         COMMAND magda_juce_tests "Multiband Curve View Tests")
