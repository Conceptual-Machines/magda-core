# Test sources
set(TEST_SOURCES
    test_audio_bridge.cpp
    test_vst3_preset.cpp
    test_audiobridge_track_limit_bug.cpp
    test_audiobridge_plugin_cleanup_bug.cpp
    test_auto_tempo.cpp
    test_audio_clip_stretch.cpp
    test_clip_bpm_invariant.cpp
    test_audio_source_length.cpp
    test_audio_export.cpp
    test_audio_to_midi.cpp
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
    test_post_fx_chain.cpp
    test_group_tracks_and_macros.cpp
    test_parameter_utils.cpp
    test_openai_url.cpp
    test_update_checker.cpp
    test_plugin_loading.cpp
    test_plugin_format.cpp
    test_plugin_window_manager.cpp
    test_plugin_capabilities.cpp
    test_plugin_preferences.cpp
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
    test_project_document_adapters.cpp
    test_session_clip_playback.cpp
    test_sidechain_triggers.cpp
    test_curve_snapshot.cpp
    test_automation_curve_simplifier.cpp
    test_scan_report.cpp
    test_drum_grid_plugin.cpp
    test_velocity_lane_utils.cpp
    test_grid_constants.cpp
    test_ui_scale.cpp
    test_media_explorer_preview_state.cpp
    test_timeline_loop_activation.cpp
    test_multi_out_tracks.cpp
    test_freeze_track.cpp
    test_dsl_add_chord.cpp
    test_dsl_add_arpeggio.cpp
    test_dsl_track_workflow.cpp
    test_scale_detection.cpp
    test_step_clock.cpp
    test_string_table.cpp
    test_mono_note_processor.cpp
    test_sends_and_track_deletion.cpp
    test_master_track_serialization.cpp
    test_automation_agent.cpp
    test_mix_analysis_agent.cpp
    test_mix_analysis_audio.cpp
    test_console_routing.cpp
    test_conversation_store.cpp
    test_track_reorder.cpp
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
    # Gesture router tests (epic:command-registry -- #21)
    test_gesture_router.cpp
    # Key mapping persistence tests (epic:command-registry -- #20)
    test_key_mapping_store.cpp
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
    # Mixing pt2 (issue #1388) - per-track loudness/level/stereo measurement DSP
    test_track_measurer.cpp
    # Mixing pt2 (issue #1390) - inter-track frequency masking detector
    test_masking_detector.cpp
    # Mixing pt2 (issue #1390) - per-track band spectrum (FFT bin -> band mapping)
    test_band_spectrum.cpp
    # Media database (issue #768) — Phase A: SQLite skeleton
    test_media_database.cpp
    # Media database (issue #768) — Phase B: scanner + path rules
    test_media_db_scan.cpp
    test_media_db_path_rules.cpp
    # Media database (issue #768) — Phase C: audio features
    test_media_db_audio_features.cpp
    # Media database (issue #768) — Phase D: CLAP audio encoder + mel
    test_media_db_clap_encoder.cpp
    # Media database (issue #768) — Phase D2: CLAP text encoder
    test_media_db_clap_text_encoder.cpp
    # Media database (issue #768) — Phase D3: RoBERTa tokenizer
    test_media_db_roberta_tokenizer.cpp
    # Media database (issue #768) — Phase E1: MediaDbIndexer
    test_media_db_indexer.cpp
    # Media database (issue #768) — Phase E2: MediaDbQuery (hybrid search)
    test_media_db_query.cpp
    # Media database (issue #1319) — CLAP zero-shot tagging
    test_media_db_zero_shot_tags.cpp
    test_limiter_dsp.cpp
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
    # JUCE (core + gui_basics) comes transitively from magda_daw, compiled once
    # there; direct links would recompile the JUCE stack into this target.
)

# Include directories
target_include_directories(magda_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Paths for the exploratory mix-analysis harness (hidden [.] tests): the repo
# root (to load .env) and the gitignored audio fixtures dir.
target_compile_definitions(magda_tests
    PRIVATE
    MAGDA_REPO_ROOT="${CMAKE_SOURCE_DIR}"
    MAGDA_AUDIO_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/audio"
    # The shared test engine must NOT auto-wire the live Tempo lane sync: it
    # attaches to the AutomationManager singleton and would perturb other tests.
    # TempoLaneSync is tested directly in test_tempo_lane_sync_juce.cpp instead.
    MAGDA_NO_AUTO_TEMPO_LANE_SYNC=1
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
    VERSION "${MAGDA_BUNDLE_VERSION}"
    COMPANY_NAME "Conceptual Machines"
    PRODUCT_NAME "Magda JUCE Tests"
    # Pin a space-free bundle ID; the company name alone would derive
    # 'com.Conceptual Machines.magda_juce_tests', which JUCE rejects.
    BUNDLE_ID "com.MAGDA.MagdaJuceTests"
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
    test_midi_bridge_note_events_juce.cpp
    test_section_scoped_device_ids_juce.cpp
    test_clip_inspector_juce.cpp
    test_multiband_curve_view_juce.cpp
    test_timeline_extreme_zoom_paint_juce.cpp
    test_arrange_beat_native_juce.cpp
    test_tempo_map_juce.cpp
    test_tempo_lane_bridge_juce.cpp
    test_tempo_lane_sync_juce.cpp
    ../magda/daw/ui/components/chain/compiled/CompiledMultibandCurveView.cpp
    ../magda/daw/ui/dialogs/ParameterConfigDialog.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorInit.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorSections.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorLifecycle.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorLayout.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorSelection.cpp
    ../magda/daw/ui/panels/content/inspector/clip/ClipInspectorState.cpp
    ../magda/daw/ui/panels/content/inspector/clip/sections/ChordProgressionSection.cpp
    ../magda/daw/ui/panels/content/inspector/clip/sections/ClipFadesSection.cpp
    ../magda/daw/ui/panels/content/inspector/clip/sections/ClipTakesSection.cpp
    ../magda/daw/ui/components/common/BarsBeatsTicksLabel.cpp
    ../magda/daw/ui/components/common/ValueLabelControl.cpp
    ../magda/daw/ui/components/common/DraggableValueLabel.cpp
    ../magda/daw/ui/components/common/SvgButton.cpp
    ../magda/daw/ui/themes/CursorManager.cpp
    ../magda/daw/ui/themes/DarkTheme.cpp
    ../magda/daw/ui/themes/FontManager.cpp
    test_automation_modes_juce.cpp
    test_arrangement_transport_loop_juce.cpp
    test_external_plugin_state_restore_juce.cpp
    test_chord_progression_converter_juce.cpp
    ../magda/daw/ui/components/chain/slot/StepSequencerClipExport.cpp
)

# Link required libraries for JUCE tests
target_link_libraries(magda_juce_tests
    PRIVATE
    magda_daw
    magda_agents
    # JUCE (core/events/gui_basics) comes transitively from magda_daw, compiled
    # once there; direct links would recompile the JUCE stack into this target.
    # Linux: juce_gui_extra is pulled in transitively via magda_daw's
    # JUCE_MODULE_AVAILABLE flags (so its .cpp gets compiled into this
    # target). Its compilation requires GTK / WebKit headers — expose
    # the same pkgconfig deps the GUI app uses, plus curl.
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

target_compile_definitions(magda_juce_tests PRIVATE MAGDA_ENABLE_TEST_HOOKS=1
    MAGDA_NO_AUTO_TEMPO_LANE_SYNC=1)

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
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/dialogs
)

add_test(NAME arrangement_transport_loop_juce
         COMMAND magda_juce_tests "Arrangement Transport Loop Tests")

add_test(NAME multiband_curve_view_juce
         COMMAND magda_juce_tests "Multiband Curve View Tests")

add_test(NAME timeline_extreme_zoom_paint_juce
         COMMAND magda_juce_tests "Timeline Extreme Zoom Paint Tests")

add_test(NAME arrange_beat_native_juce
         COMMAND magda_juce_tests "Arrange Beat Native Tests")

add_test(NAME tempo_map_juce
         COMMAND magda_juce_tests "Tempo Map Tests")

add_test(NAME tempo_lane_bridge_juce
         COMMAND magda_juce_tests "Tempo Lane Bridge Tests")

add_test(NAME tempo_lane_sync_juce
         COMMAND magda_juce_tests "Tempo Lane Sync Tests")

add_test(NAME midi_bridge_note_events_juce
         COMMAND magda_juce_tests "MidiBridge Note Events Tests")
add_test(NAME external_plugin_state_restore_juce
         COMMAND magda_juce_tests "External Plugin State Restore Tests")

add_test(NAME section_scoped_device_ids_juce
         COMMAND magda_juce_tests "Section-Scoped Device IDs")

add_test(NAME chord_progression_converter_juce
         COMMAND magda_juce_tests "Chord Progression Converter Tests")

# Both test executables link magda_daw, which links ONNX Runtime. On Windows the
# loader only searches the exe's directory, the cwd, and PATH, so without a copy
# next to the test exe it resolves whatever stray onnxruntime.dll happens to be
# on the runner's PATH (a 1.17 system DLL on CI) instead of our pinned build.
# The version mismatch makes the C++ API's GetApi() return null and the first
# ORT call SIGSEGVs. Copy the pinned DLLs next to each test exe, mirroring the
# app bundle step in magda/daw/CMakeLists.txt.
if(MAGDA_HAVE_CLAP AND WIN32)
    foreach(test_target magda_tests magda_juce_tests)
        add_custom_command(TARGET ${test_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll"
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:${test_target}>"
            COMMENT "Copying ONNX Runtime DLLs alongside ${test_target}")
    endforeach()
endif()

# magda_daw also links libxml2 (DAWproject validation), a DLL on Windows. Mirror
# the app's runtime-DLL copy so DawProjectValidator tests find libxml2.dll (+ its
# transitive deps) next to each test exe instead of faulting at load. Unlike the
# ONNX copy this is not gated on CLAP, since libxml2 is always linked.
if(WIN32)
    foreach(test_target magda_tests magda_juce_tests)
        add_custom_command(TARGET ${test_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:${test_target}>"
                "$<TARGET_FILE_DIR:${test_target}>"
            COMMAND_EXPAND_LISTS
            COMMENT "Copying runtime DLLs (libxml2 + deps) alongside ${test_target}")
    endforeach()
endif()
