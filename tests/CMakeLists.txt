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
    test_chain_info_copy.cpp
    test_drum_grid_pad_commands.cpp
    test_pad_path_types.cpp
    test_drum_grid_pads.cpp
    test_drum_grid_pads_corpus.cpp
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
    test_hash_combine.cpp
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
    # Real projects and presets saved by released versions (#2079). Model-level:
    # the load, the migrations it runs and the links it repoints need no engine.
    # The v1 -> v2 device state conversion does, and lives in
    # test_legacy_corpus_migration_juce.cpp.
    test_legacy_corpus.cpp
    test_persisted_enum_pins.cpp
    test_device_param_migrations.cpp
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
    # The prompts' worked examples must satisfy the validator (Faust instrument
    # [idx:N] report)
    test_faust_prompt_examples.cpp
    test_dsl_project_commands.cpp
    test_dsl_rack_commands.cpp
    test_scale_detection.cpp
    test_step_clock.cpp
    test_string_table.cpp
    # Guards the juce::String(const char*) ASCII-decode trap (#1858 follow-up)
    test_non_ascii_string_literals.cpp
    # Arrangement pointer hit tester (#1719)
    test_arrangement_hit_tester.cpp
    # Curve-view readout stays inside the plot (#2072)
    test_curve_label_layout.cpp
    # Transport sections measure themselves instead of estimating (#2071/#2074).
    # TransportLayout builds into magda_daw_app, so pull its implementation in.
    test_transport_layout.cpp
    ../magda/daw/ui/panels/TransportLayout.cpp
    # Panel tab layout survives a restart (#2103). The panel sources live in
    # magda_daw_app rather than the magda_daw library, so pull them in directly.
    test_panel_tab_persistence.cpp
    ../magda/daw/ui/panels/state/PanelController.cpp
    ../magda/daw/ui/panels/content/PanelContent.cpp
    # Keep test runs out of the developer's real config.json. The redirect must
    # be linked into every test binary, and the guard test proves it took.
    TestConfigRedirect.cpp
    test_config_path_redirect.cpp
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
    # What each track type declares about itself (#2172)
    test_track_type_traits.cpp
    # New tracks take their post-FX fader side from the preference (#2094)
    test_post_fx_fader_default.cpp
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
    # OSC control surfaces (issue #1757)
    test_osc_address.cpp
    test_osc_packet.cpp
    test_osc_router.cpp
    test_osc_command_sink.cpp
    test_osc_bindings.cpp
    test_osc_feedback.cpp
    # Relative transport seek (#1987)
    test_transport_seek.cpp
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
    test_remote_permissions.cpp
    test_remote_permission_conformance.cpp
    test_remote_service.cpp
    test_remote_changes.cpp
    test_remote_subscriptions.cpp
    test_remote_model_bridge.cpp
    test_remote_mcp.cpp
    test_remote_mcp_server.cpp
    test_remote_mcp_bridge.cpp
    test_mcp_sse_parser.cpp
    test_remote_websocket_server.cpp
    test_remote_api_host.cpp
    test_device_api.cpp
    # Rack and chain addressing at depth through the facade (issue #1993)
    test_track_api_nested_racks.cpp
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
    # The note-range gate a pad chain needs (#2200).
    list(APPEND TEST_SOURCES test_plan_note_gate.cpp)
    list(APPEND TEST_SOURCES test_plan_pad_rack.cpp)
    list(APPEND TEST_SOURCES test_plan_diff.cpp)
    # Rack nesting, recursion and op-key identity (#2137). Reaches the parameter
    # table as well as the plan, because the nearest-scope rule the two resolve
    # against is what has to agree across a nesting change.
    list(APPEND TEST_SOURCES test_plan_rack_nesting.cpp)
    list(APPEND TEST_SOURCES test_plan_crossfade.cpp)
    # Plan goldens (#2076). Fixtures and runner both reach into magda_engine.
    list(APPEND TEST_SOURCES PlanGoldenFixtures.cpp)
    list(APPEND TEST_SOURCES test_plan_goldens.cpp)
    # Differ property tests over random edit sequences (#2077). The vocabulary
    # and the session the properties publish into are model-only, so they build
    # here beside the runner rather than needing a te::Edit.
    list(APPEND TEST_SOURCES PlanEditSequence.cpp)
    list(APPEND TEST_SOURCES PlanEditHarness.cpp)
    list(APPEND TEST_SOURCES test_plan_edit_properties.cpp)
    list(APPEND TEST_SOURCES test_engine_session.cpp)
    # The parameter lane (#2116). The lanes are fed by hand, so the precedence
    # rules are testable before anything writes them.
    list(APPEND TEST_SOURCES test_param_lane.cpp)
    # The parameter table (#2117): addressing, the link graph and publication.
    list(APPEND TEST_SOURCES test_param_table.cpp)
    # Automation lanes baked into the parameter lane (#2118).
    list(APPEND TEST_SOURCES test_automation_bake.cpp)
    # The LFO engine: shapes, rates, triggers and gates (#2119).
    list(APPEND TEST_SOURCES test_mod_lfo.cpp)
    # The remaining three modifier engines, and the feeds two of them need
    # (#2120): the envelope's gate, the walk, and the follower's source.
    list(APPEND TEST_SOURCES test_mod_adsr.cpp)
    list(APPEND TEST_SOURCES test_mod_random.cpp)
    list(APPEND TEST_SOURCES test_mod_follower.cpp)
    list(APPEND TEST_SOURCES test_mod_feeds.cpp)
    # Macros at track, rack and device scope (#2121): which array a path
    # names, nested racks, and a macro reaching a modifier's rate.
    list(APPEND TEST_SOURCES test_param_macros.cpp)
    # What the UI reads back (#2122): the modifier's output and the parameter's
    # resolved value, published by the block that produced them.
    list(APPEND TEST_SOURCES test_value_taps.cpp)
    list(APPEND TEST_SOURCES test_tempo_map.cpp)
    list(APPEND TEST_SOURCES test_transport_clock.cpp)
    list(APPEND TEST_SOURCES test_click_generator.cpp)
    list(APPEND TEST_SOURCES test_prefetch_stream.cpp)
    list(APPEND TEST_SOURCES test_source_readers.cpp)
    list(APPEND TEST_SOURCES test_streaming_audio_source.cpp)
    list(APPEND TEST_SOURCES test_file_audio_source.cpp)
    list(APPEND TEST_SOURCES test_offline_render.cpp)
    list(APPEND TEST_SOURCES test_engine_taps.cpp)
    list(APPEND TEST_SOURCES test_clip_snapshot.cpp)
    list(APPEND TEST_SOURCES test_clip_voice.cpp)
    list(APPEND TEST_SOURCES test_clip_voice_pool.cpp)
    list(APPEND TEST_SOURCES test_clip_stretch.cpp)
    list(APPEND TEST_SOURCES test_clip_warp.cpp)
    list(APPEND TEST_SOURCES test_midi_clip_playback.cpp)
    list(APPEND TEST_SOURCES test_groove_template.cpp)
    list(APPEND TEST_SOURCES test_transient_detector.cpp)
    list(APPEND TEST_SOURCES test_source_loop_info.cpp)

    # The null-diff corpus (#2040). The comparator and the material it plays are
    # model-only and live here, where they can be tested against known-bad pairs
    # on their own; the two legs and the runner need a te::Edit and so live in
    # magda_juce_tests.
    list(APPEND TEST_SOURCES NullDiffMaterial.cpp)
    list(APPEND TEST_SOURCES NullDiffCompare.cpp)
    list(APPEND TEST_SOURCES NullDiffCorpus.cpp)
    list(APPEND TEST_SOURCES NullDiffNativeLeg.cpp)
    list(APPEND TEST_SOURCES test_null_diff_compare.cpp)
    list(APPEND TEST_SOURCES test_null_diff_corpus.cpp)
    list(APPEND TEST_SOURCES test_null_diff_native_leg.cpp)

    # The .mgd fixture rig (#2173): a real project turned into a case. Model
    # only, like the corpus beside it -- the load produces model values and this
    # slice renders nothing, so neither leg is involved.
    list(APPEND TEST_SOURCES MgdFixture.cpp)
    list(APPEND TEST_SOURCES MgdFixtures.cpp)
    list(APPEND TEST_SOURCES test_mgd_fixture_rig.cpp)

    # What the legacy projects would cost as render cases (#2081). A survey
    # rather than a corpus: it declares no tier and asserts no residual, and
    # reports what stands between each project and being a case.
    list(APPEND TEST_SOURCES test_real_project_survey.cpp)

    # The device layer under the engine (#2174): the app's own catalogs asked
    # for a Device op's device, and a shipped device rendered through the
    # adapter. The engine's half alone -- what the two hosts do with one device
    # is the corpus's question, in magda_juce_tests.
    list(APPEND TEST_SOURCES test_engine_device_layer.cpp)

    # The block-size invariance gate (#2078). Model-only for the same reason the
    # native leg is: the claim is the native engine's alone, and the incumbent
    # owes nobody block-size invariance. In CI rather than in a nightly, because
    # a gate that runs after the merge is a report rather than a gate.
    list(APPEND TEST_SOURCES test_null_diff_block_size.cpp)

    # The DAWproject cross-check (#2080). Native against native: each corpus
    # project is exported, reimported, and required to compile to the same plan
    # and render to the same samples. Model-only because both sides are the
    # native engine and the export path is plain serialization.
    list(APPEND TEST_SOURCES DawProjectRoundTrip.cpp)
    list(APPEND TEST_SOURCES test_dawproject_round_trip.cpp)
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
    # cpp-httplib ships both an HTTP and a WebSocket client, so the transport
    # tests in test_remote_websocket_server.cpp and test_remote_mcp_server.cpp
    # drive real sockets without a test-only dependency. magda_daw links it
    # PRIVATE, so this target needs its own.
    httplib::httplib
    # JUCE (core + gui_basics) comes transitively from magda_daw, compiled once
    # there; direct links would recompile the JUCE stack into this target.
)

if(MAGDA_BUILD_NATIVE_ENGINE)
    target_link_libraries(magda_tests PRIVATE magda_engine magda_engine_devices)
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
    # Plan goldens (#2076) live in the source tree, so an intended change to
    # the compiler shows up as a reviewable diff rather than a rewritten
    # build artefact.
    MAGDA_PLAN_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/goldens/plan"
    # The shared test engine must NOT auto-wire the live Tempo lane sync: it
    # attaches to the AutomationManager singleton and would perturb other tests.
    # TempoLaneSync is tested directly in test_tempo_lane_sync_juce.cpp instead.
    MAGDA_NO_AUTO_TEMPO_LANE_SYNC=1
    # Friend access for test_llama_model_manager.cpp. The define also happens to
    # arrive transitively via magda_daw's INTERFACE re-export of its own
    # COMPILE_DEFINITIONS; set it explicitly so this target does not depend on
    # that re-export staying broad.
    MAGDA_ENABLE_TEST_HOOKS=1
    # test_remote_mcp_bridge.cpp forks the real bridge binary: the thing it
    # verifies is the process boundary, so there is nothing to link. Taking the
    # path from the target rather than reconstructing it keeps the test pointed
    # at the binary this build produced.
    MAGDA_MCP_BRIDGE_BINARY="$<TARGET_FILE:magda_mcp_bridge>"
    # The legacy corpus (#2079) and the frozen device parameter order it checks
    # saved indices against (#1887). Both live in the source tree: the corpus is
    # real user files that must never be regenerated, and an intended change to
    # the frozen order has to show up as a reviewable diff.
    MAGDA_LEGACY_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/legacy"
    # The root the .mgd fixtures name a file under (#2173). One level above the
    # legacy corpus rather than inside it, because a fixture can be a legacy
    # project reused or one saved for the render corpus, and where it lives is
    # part of what a manifest says.
    MAGDA_TEST_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus"
    MAGDA_DEVICE_PARAM_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/device_param_schema.txt"
)

# The bridge is exec'd, not linked, so nothing in the link graph would build it.
add_dependencies(magda_tests magda_mcp_bridge)

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
    test_assertion_watch_juce.cpp
    test_midi_recording_juce.cpp
    test_clip_sync_integration_juce.cpp
    test_signalsmith_loop_juce.cpp
    test_arranger_launcher_switching_juce.cpp
    test_node_id_collision_juce.cpp
    test_transport_seek_juce.cpp
    test_session_slot_recording_juce.cpp
    test_input_monitor_track_routing_juce.cpp
    test_controller_track_level_write_juce.cpp
    # OSC control surfaces (issue #1757): a live receive thread and real packets
    test_osc_service_juce.cpp
    test_post_fx_fader_order_juce.cpp
    test_track_midi_recording_preview_juce.cpp
    test_timeline_loop_activation_juce.cpp
    test_midi_signal_routing_juce.cpp
    test_drum_grid_serialization_juce.cpp
    test_sampler_relocate_juce.cpp
    test_device_services_juce.cpp
    test_device_param_schema_juce.cpp
    # pluginState v1 -> v2 over the legacy corpus (#2079). Here rather than in
    # magda_tests because the conversion reads the values off a live plugin.
    test_legacy_corpus_migration_juce.cpp
    test_device_state_schema_juce.cpp
    test_convolution_device_juce.cpp
    test_master_sidechain_juce.cpp
    test_midi_bridge_note_events_juce.cpp
    test_section_scoped_device_ids_juce.cpp
    test_remote_service_live_juce.cpp
    # Path-addressed racks/chains against the real TrackManager (issue #1993)
    test_track_api_nested_racks_live_juce.cpp
    test_remote_websocket_live_juce.cpp
    test_clip_inspector_juce.cpp
    test_multi_clip_resize_preview_juce.cpp
    test_clip_paint_juce.cpp
    test_clip_nudge_juce.cpp
    # A .mid dropped on the chord track arrives as chords
    test_chord_track_file_drop_juce.cpp
    # Files dropped straight onto a Drum Grid track (#2172)
    test_drum_grid_file_drop_juce.cpp
    test_multiband_curve_view_juce.cpp
    # The transport layout against the widths the shipped fonts really produce
    # (#2071). Needs a graphics context, so it lives here rather than in
    # magda_tests, which asserts the same layout as pure arithmetic.
    test_transport_layout_juce.cpp
    ../magda/daw/ui/panels/TransportLayout.cpp
    ../magda/daw/ui/panels/TransportTextWidths.cpp
    # Keep test runs out of the developer's real config.json (see the copy in
    # magda_tests): RemoteApiHost persists grants via Config::save(). The guard
    # test is duplicated per target because each uses its own framework.
    TestConfigRedirect.cpp
    test_config_path_redirect_juce.cpp
    # Boolean param cells follow value-only refreshes (#2072 follow-up).
    # ParamSlotComponent builds into magda_daw_app, so the test target needs it
    # and the param helpers it calls into compiled in directly.
    test_param_slot_boolean_sync_juce.cpp
    # A discrete cell's dropdown must not write the parameter on rebuild, and
    # must map an external plugin's TE-native value onto its choices.
    test_param_slot_discrete_combo_juce.cpp
    ../magda/daw/ui/components/chain/params/ParamSlotComponent.cpp
    ../magda/daw/ui/components/chain/params/ParamWidgetSetup.cpp
    ../magda/daw/ui/components/chain/params/ParamLinkResolver.cpp
    ../magda/daw/ui/components/chain/params/ParamLinkMenu.cpp
    ../magda/daw/ui/components/chain/params/ModulationBakeAction.cpp
    ../magda/daw/ui/components/chain/params/ParamModulationPainter.cpp
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
    # PanelController persists its tab layout by content-type id, which lives here
    ../magda/daw/ui/panels/content/PanelContent.cpp
    ../magda/daw/ui/components/common/BarsBeatsTicksLabel.cpp
    ../magda/daw/ui/components/common/ValueLabelControl.cpp
    ../magda/daw/ui/components/common/DraggableValueLabel.cpp
    ../magda/daw/ui/components/common/SvgButton.cpp
    ../magda/daw/ui/components/common/InternalFileDrag.cpp
    ../magda/daw/ui/themes/CursorManager.cpp
    ../magda/daw/ui/themes/FontManager.cpp
    test_arrangement_row_alignment_juce.cpp
    test_automation_modes_juce.cpp
    test_arrangement_transport_loop_juce.cpp
    test_external_plugin_state_restore_juce.cpp
    test_chord_progression_converter_juce.cpp
    test_audio_driver_type_selection_juce.cpp
    test_tempo_sequence_ripple_juce.cpp
    ../magda/daw/ui/components/chain/slot/StepSequencerClipExport.cpp
)

# The null-diff corpus (#2040). It lives here rather than in magda_tests because
# the incumbent leg is a te::Edit, and Edits in that target take later tests down
# with them. The comparator, the material and the corpus itself are model-only
# and are compiled into both, so they can also be tested on their own.
if(MAGDA_BUILD_NATIVE_ENGINE)
    target_sources(magda_juce_tests PRIVATE
        NullDiffMaterial.cpp
        NullDiffCompare.cpp
        NullDiffCorpus.cpp
        NullDiffNativeLeg.cpp
        NullDiffTeLeg.cpp
        test_null_diff_juce.cpp
    )
endif()

# Link required libraries for JUCE tests
target_link_libraries(magda_juce_tests
    PRIVATE
    magda_daw
    # ClipComponent's context menu reaches the AI settings dialog, which lives
    # on the agents library.
    magda_agents
    # cpp-httplib's WebSocket client, for driving the transport against the live
    # facade in test_remote_websocket_live_juce.cpp. magda_daw links it PRIVATE.
    httplib::httplib
    # JUCE (core/events/gui_basics) comes transitively from magda_daw, compiled
    # once there; direct links would recompile the JUCE stack into this target.
    # Linux: juce_gui_extra is pulled in transitively via magda_daw's
    # JUCE_MODULE_AVAILABLE flags (so its .cpp gets compiled into this
    # target). Its compilation requires GTK / WebKit headers — expose
    # the same pkgconfig deps the GUI app uses, plus curl.
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

# The native engine, for the null-diff corpus (#2040). This is the one target
# that links both it and Tracktion, which is the whole point of it: the corpus
# renders one project through each and compares.
#
# magda_engine_devices with it (#2174): the device layer the native leg binds a
# Device op through, so both legs run the app's own devices rather than a pair
# written for the corpus.
if(MAGDA_BUILD_NATIVE_ENGINE)
    target_link_libraries(magda_juce_tests PRIVATE magda_engine magda_engine_devices)
endif()

target_compile_definitions(magda_juce_tests PRIVATE MAGDA_ENABLE_TEST_HOOKS=1
    MAGDA_NO_AUTO_TEMPO_LANE_SYNC=1
    # JUCE_LOG_ASSERTIONS is set on magda_daw instead, gated on MAGDA_BUILD_TESTS:
    # that is where Tracktion is compiled, and an assertion's logging is decided
    # where the macro expands rather than where the test that wants to read it
    # lives. Setting it here reaches nothing.
    # Frozen device parameter order (#1887) lives in the source tree so an
    # intentional change shows up as a reviewable diff.
    MAGDA_DEVICE_PARAM_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/device_param_schema.txt"
    # The legacy corpus (#2079): the JUCE suite converts the pre-v2 device state
    # its projects carry, which needs a live plugin to read the values off.
    MAGDA_LEGACY_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/legacy")

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

add_test(NAME config_path_redirect_juce
         COMMAND magda_juce_tests "Config Path Redirect Tests")

add_test(NAME param_slot_boolean_sync_juce
         COMMAND magda_juce_tests "Param Slot Boolean Sync Tests")

add_test(NAME param_slot_discrete_combo_juce
         COMMAND magda_juce_tests "Param Slot Discrete Combo Tests")

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

# The null-diff corpus (#2040): every case rendered through both engines. Two
# entries because they fail for different reasons and one of them is much
# cheaper: a mapping disagreement moves every clip in every project, so it is
# worth seeing on its own before any waveform is compared.
if(MAGDA_BUILD_NATIVE_ENGINE)
    add_test(NAME null_diff_maps_juce
             COMMAND magda_juce_tests "Null Diff Maps")
    add_test(NAME null_diff_corpus_juce
             COMMAND magda_juce_tests "Null Diff Corpus")
endif()

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

# The facade's path-addressed rack/chain surface against the real TrackManager.
add_test(NAME track_api_nested_racks_live_juce
         COMMAND magda_juce_tests "Track API Nested Racks Live")

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
