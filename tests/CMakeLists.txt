# Test sources
set(TEST_SOURCES
    # Must be in both test targets: a debug-CRT dialog blocks a headless run
    # until the CI job times out, hiding the diagnostic that caused it.
    windows_no_dialogs.cpp
    test_audio_bridge.cpp
    test_vst3_preset.cpp
    test_audiobridge_track_limit_bug.cpp
    test_audiobridge_plugin_cleanup_bug.cpp
    test_auto_tempo.cpp
    test_audio_clip_stretch.cpp
    test_clip_bpm_invariant.cpp
    test_source_pool.cpp
    test_clip_event_model.cpp
    test_clip_schema_migration.cpp
    test_audio_source_length.cpp
    test_audio_export.cpp
    test_offline_render_contract.cpp
    test_audio_to_midi.cpp
    test_command.cpp
    test_command_model.cpp
    test_command_model_downloader.cpp
    test_unigram_tokenizer.cpp
    test_command_model_onnx.cpp
    test_interfaces.cpp
    test_midi_clip_sync.cpp
    test_midi_clip_split.cpp
    test_midi_legato.cpp
    test_midi_offset.cpp
    test_pianoroll_note_position.cpp
    test_nested_racks.cpp
    test_rack_audio.cpp
    test_modulation.cpp
    test_control_target.cpp
    test_tempo_sequence_ripple_math.cpp
    test_chain_fingerprint.cpp
    test_chain_routing_model.cpp
    test_post_fx_chain.cpp
    test_group_tracks_and_macros.cpp
    test_parameter_utils.cpp
    test_openai_url.cpp
    test_llm_tool_calls.cpp
    test_update_checker.cpp
    test_plugin_loading.cpp
    test_plugin_format.cpp
    test_internal_plugin_registry.cpp
    # Engine-neutral internal device state schema v2 (issue #1887)
    test_device_state_codec.cpp
    test_device_sdk_handles.cpp
    test_external_insert_registry.cpp
    test_external_insert_enablement.cpp
    test_insert_capture.cpp
    test_plugin_window_manager.cpp
    test_plugin_capabilities.cpp
    test_plugin_preferences.cpp
    test_plugin_browser_metadata_merge.cpp
    test_plugin_metadata_store.cpp
    test_prune_missing_plugins.cpp
    test_known_plugin_list_duplicates.cpp
    test_device_parameter_pagination.cpp
    test_device_ui_context.cpp
    test_levels_ui.cpp
    # LevelsUI builds into magda_daw_app, so magda_tests needs its implementation
    # directly - along with FontManager, which its themed button LookAndFeel pulls in.
    ../magda/daw/ui/components/chain/custom_ui/LevelsUI.cpp
    ../magda/daw/ui/themes/FontManager.cpp
    test_chain_node_path_drag.cpp
    # ChainNodePathDrag builds into magda_daw_app, so magda_tests needs it directly.
    ../magda/daw/ui/components/chain/ChainNodePathDrag.cpp
    test_momentary_param_button.cpp
    test_delta_solo.cpp
    test_waveform_editor_absolute_mode.cpp
    test_clip_batch_edit.cpp
    test_clip_commands.cpp
    test_clip_crossfades.cpp
    test_clip_occlusion.cpp
    # Ghost clips / link groups (issue #12)
    test_ghost_clips.cpp
    test_clip_resize_operations.cpp
    test_midi_clip_resize_left.cpp
    test_looped_waveform_tile.cpp
    test_project_serialization.cpp
    test_project_document_adapters.cpp
    # Unsaved-changes tracking across the undo stack and project boundaries
    test_project_dirty_tracking.cpp
    test_session_clip_playback.cpp
    test_sidechain_triggers.cpp
    test_sidechain_device.cpp
    test_curve_snapshot.cpp
    test_curve_editor_preview.cpp
    test_automation_curve_simplifier.cpp
    # Bake modulation to automation (issue #162)
    test_modulation_baker.cpp
    # Clip-based automation lanes: gap-hold + bake flattener (issue #1087)
    test_automation_clip_lanes.cpp
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
    test_agent_device_catalog.cpp
    test_chord_agent.cpp
    test_faust_agent.cpp
    test_dsl_project_commands.cpp
    test_dsl_rack_commands.cpp
    test_scale_detection.cpp
    test_step_clock.cpp
    test_string_table.cpp
    # Arrangement pointer hit tester (#1719)
    test_arrangement_hit_tester.cpp
    # Keyboard clip nudging (#1957)
    test_clip_nudge.cpp
    test_mono_note_processor.cpp
    test_sends_and_track_deletion.cpp
    # Internal track-to-track routing (issue #1690)
    test_internal_track_routing.cpp
    test_master_track_serialization.cpp
    test_utility_width_migration.cpp
    test_legacy_device_aliases.cpp
    test_automation_agent.cpp
    test_mix_analysis_agent.cpp
    test_mix_analysis_audio.cpp
    test_console_routing.cpp
    test_conversation_store.cpp
    test_agent_runtime.cpp
    test_console_agent_orchestrator.cpp
    test_midi_context.cpp
    test_llama_model_manager.cpp
    test_track_reorder.cpp
    test_automation_modes.cpp
    test_automation_stepped_targets.cpp
    test_automation_singleton.cpp
    test_instruction_executor_context.cpp
    # Faust pool refactor (Phase 1: metadata parser)
    test_faust_metadata_parser.cpp
    test_faust_ui_harvester.cpp
    test_faust_device_layout.cpp
    # Device layouts build into magda_daw_app, so layout tests need their implementations directly.
    ../magda/daw/ui/components/chain/layout/StandardDeviceLayout.cpp
    ../magda/daw/ui/components/chain/layout/FaustDeviceLayout.cpp
    ../magda/daw/ui/components/chain/layout/CompiledFaustDeviceLayout.cpp
    ../magda/daw/ui/components/chain/slot/DeviceSlotParamLayoutFactory.cpp
    test_faust_param_pool.cpp
    test_faust_param_info.cpp
    test_faust_patch_info.cpp
    test_faust_custom_view.cpp
    # Runtime execution backend (interp / wasm+wasmtime), issue #1382
    test_faust_backend.cpp
    # Parameter alias system tests (PR 1)
    test_param_name_normalize.cpp
    test_alias_parser.cpp
    # Parameter alias system tests (PR 2)
    test_alias_registry.cpp
    # Parameter alias system tests (PR 3)
    test_alias_resolver.cpp
    # Track / master level writes driven by control surfaces
    test_track_level_control_writes.cpp
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
    test_menu_manager.cpp
    # MenuManager builds into magda_daw_app, so magda_tests needs its implementation directly.
    ../magda/daw/ui/windows/MenuManager.cpp
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
    test_remote_api_contract.cpp
    test_remote_service.cpp
    test_remote_changes.cpp
    test_remote_model_bridge.cpp
    test_device_api.cpp
    test_automation_api_surface.cpp
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
    # Stem separation (issue #1288) — HPSS DSP engine + Demucs OLA geometry
    test_hpss_separator.cpp
    test_demucs_chunking.cpp
    test_demucs_separator.cpp
    test_spleeter_separator.cpp
)

# Native audio engine (epic #1882). The engine is a dark target with no TE and
# no UI, but its compiler is exercised here: the model fixtures it compiles are
# plain value types, so the plan tests are ordinary model-level Catch2 tests.
if(MAGDA_BUILD_NATIVE_ENGINE)
    list(APPEND TEST_SOURCES test_render_plan_compiler.cpp)
    list(APPEND TEST_SOURCES test_render_plan_executor.cpp)
    list(APPEND TEST_SOURCES test_parallel_executor.cpp)
    list(APPEND TEST_SOURCES test_plan_layout.cpp)
    list(APPEND TEST_SOURCES test_plan_diff.cpp)
    list(APPEND TEST_SOURCES test_plan_crossfade.cpp)
    list(APPEND TEST_SOURCES test_engine_session.cpp)
    list(APPEND TEST_SOURCES test_tempo_map.cpp)
    list(APPEND TEST_SOURCES test_transport_clock.cpp)
    list(APPEND TEST_SOURCES test_click_generator.cpp)
    list(APPEND TEST_SOURCES test_prefetch_stream.cpp)
    list(APPEND TEST_SOURCES test_streaming_audio_source.cpp)
    list(APPEND TEST_SOURCES test_file_audio_source.cpp)
    list(APPEND TEST_SOURCES test_offline_render.cpp)
    list(APPEND TEST_SOURCES test_engine_taps.cpp)
endif()

# Create test executable
add_executable(magda_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(magda_tests
    PRIVATE
    magda_daw
    magda_agents
    magda_scripting
    Catch2::Catch2WithMain
    # Faust runtime headers + the backend selection, so test_faust_backend.cpp
    # resolves to the same factory the app was built with.
    magda_faust_backend
    # JUCE (core + gui_basics) comes transitively from magda_daw, compiled once
    # there; direct links would recompile the JUCE stack into this target.
)

if(MAGDA_BUILD_NATIVE_ENGINE)
    target_link_libraries(magda_tests PRIVATE magda_engine)
endif()

# Include directories
target_include_directories(magda_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/components/chain
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
    # Friend access for test_llama_model_manager.cpp. The define also happens to
    # arrive transitively via magda_daw's INTERFACE re-export of its own
    # COMPILE_DEFINITIONS; set it explicitly so this target does not depend on
    # that re-export staying broad.
    MAGDA_ENABLE_TEST_HOOKS=1
)

# Add the test to CTest
add_test(NAME magda_unit_tests COMMAND magda_tests)

add_test(
    NAME magda_cli_headless_e2e
    COMMAND ${CMAKE_COMMAND}
        -DMAGDA_CLI=$<TARGET_FILE:magda_cli>
        -DMAGDA_CLI_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/magda_cli_headless_e2e
        -P ${CMAKE_CURRENT_SOURCE_DIR}/magda_cli_headless_e2e.cmake
)

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
    windows_no_dialogs.cpp
    test_zoom_simple.cpp
    test_export_transport_stop_juce.cpp
    test_offline_render_device_power_juce.cpp
    test_tracktion_engine_wrapper_refactoring_juce.cpp
    # test_export_no_assertion_juce.cpp  # Disabled - see issue #611 (generator device export)
    test_midi_recording_juce.cpp
    test_clip_sync_integration_juce.cpp
    test_signalsmith_loop_juce.cpp
    test_arranger_launcher_switching_juce.cpp
    test_session_slot_recording_juce.cpp
    test_input_monitor_track_routing_juce.cpp
    test_controller_track_level_write_juce.cpp
    test_track_midi_recording_preview_juce.cpp
    test_timeline_loop_activation_juce.cpp
    test_midi_signal_routing_juce.cpp
    test_drum_grid_serialization_juce.cpp
    test_device_services_juce.cpp
    test_device_param_schema_juce.cpp
    test_device_state_schema_juce.cpp
    test_convolution_device_juce.cpp
    test_master_sidechain_juce.cpp
    test_midi_bridge_note_events_juce.cpp
    test_section_scoped_device_ids_juce.cpp
    test_remote_service_live_juce.cpp
    test_clip_inspector_juce.cpp
    test_multi_clip_resize_preview_juce.cpp
    test_clip_paint_juce.cpp
    test_clip_nudge_juce.cpp
    test_multiband_curve_view_juce.cpp
    test_timeline_extreme_zoom_paint_juce.cpp
    test_repaint_invariance_juce.cpp
    test_arrange_beat_native_juce.cpp
    test_tempo_map_juce.cpp
    test_tempo_lane_bridge_juce.cpp
    test_tempo_lane_sync_juce.cpp
    test_faust_sidechain_juce.cpp
    test_faust_instrument_tempo_juce.cpp
    test_faust_instrument_voice_modes_juce.cpp
    test_runtime_theme_juce.cpp
    test_user_theme_juce.cpp
    test_syntax_highlighting_juce.cpp
    ../magda/daw/ui/code/SyntaxTheme.cpp
    ../magda/daw/ui/code/DSLTokeniser.cpp
    ../magda/daw/ui/code/FaustTokeniser.cpp
    test_device_gain_slider.cpp
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
    ../magda/daw/ui/components/clips/ClipComponent.cpp
    ../magda/daw/ui/components/tracks/TrackContentPanel.cpp
    ../magda/daw/ui/components/tracks/TrackContentPanelClipDrag.cpp
    ../magda/daw/ui/components/automation/AutomationLaneComponent.cpp
    ../magda/daw/ui/components/automation/AutomationCurveEditor.cpp
    ../magda/daw/ui/components/common/curve/CurveEditorBase.cpp
    ../magda/daw/ui/components/common/curve/CurvePointComponent.cpp
    ../magda/daw/ui/components/common/curve/CurveBezierHandle.cpp
    ../magda/daw/ui/components/common/curve/CurveTensionHandle.cpp
    ../magda/daw/ui/components/automation/AutomationClipComponent.cpp
    ../magda/daw/ui/components/waveform/ClipWaveformPainter.cpp
    ../magda/daw/ui/components/common/Toast.cpp
    ../magda/daw/ui/dialogs/AISettingsDialog.cpp
    ../magda/daw/ui/interaction/ArrangementCursorMap.cpp
    ../magda/daw/ui/panels/state/PanelController.cpp
    ../magda/daw/ui/components/common/BarsBeatsTicksLabel.cpp
    ../magda/daw/ui/components/common/ValueLabelControl.cpp
    ../magda/daw/ui/components/common/DraggableValueLabel.cpp
    ../magda/daw/ui/components/common/SvgButton.cpp
    ../magda/daw/ui/components/common/InternalFileDrag.cpp
    ../magda/daw/ui/themes/CursorManager.cpp
    ../magda/daw/ui/themes/FontManager.cpp
    test_automation_modes_juce.cpp
    test_arrangement_transport_loop_juce.cpp
    test_external_plugin_state_restore_juce.cpp
    test_chord_progression_converter_juce.cpp
    test_audio_driver_type_selection_juce.cpp
    test_tempo_sequence_ripple_juce.cpp
    ../magda/daw/ui/components/chain/slot/StepSequencerClipExport.cpp
)

# Link required libraries for JUCE tests
target_link_libraries(magda_juce_tests
    PRIVATE
    magda_daw
    # ClipComponent's context menu reaches the AI settings dialog, which lives
    # on the agents library.
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
    MAGDA_NO_AUTO_TEMPO_LANE_SYNC=1
    # Frozen device parameter order (#1887) lives in the source tree so an
    # intentional change shows up as a reviewable diff.
    MAGDA_DEVICE_PARAM_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/device_param_schema.txt")

# Include directories for JUCE tests
target_include_directories(magda_juce_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/magda/daw
    ${CMAKE_SOURCE_DIR}/magda/daw/ui
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/themes
    ${CMAKE_SOURCE_DIR}/magda/daw/ui/code
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

add_test(NAME runtime_theme_juce
         COMMAND magda_juce_tests "Runtime Theme Tests")

add_test(NAME user_theme_juce
         COMMAND magda_juce_tests "User Theme Tests")

add_test(NAME syntax_highlighting_juce
         COMMAND magda_juce_tests "Syntax Highlighting Tests")

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

add_test(NAME offline_render_device_power_juce
         COMMAND magda_juce_tests "Offline Render Device Power Tests")

add_test(NAME chord_progression_converter_juce
         COMMAND magda_juce_tests "Chord Progression Converter Tests")

add_test(NAME device_param_schema_juce
         COMMAND magda_juce_tests "Device Param Schema Freeze")

add_test(NAME device_state_schema_juce
         COMMAND magda_juce_tests "Device State Schema")

add_test(NAME convolution_device_juce
         COMMAND magda_juce_tests "Convolution Device")

# Remote API against the live facade. Runs here rather than in magda_tests
# because executing an undoable command constructs ProjectManager through
# UndoableMutationScope, and its constructor starts a JUCE timer — which needs
# an initialised message system to be valid.
add_test(NAME remote_service_live_juce
         COMMAND magda_juce_tests "Remote Service Live")

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
