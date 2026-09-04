# Test sources
set(TEST_SOURCES
    # Must be in both test targets: a debug-CRT dialog blocks a headless run
    # until the CI job times out, hiding the diagnostic that caused it.
    windows_no_dialogs.cpp
    audio/test_audio_bridge.cpp
    audio/test_vst3_preset.cpp
    audio/test_audiobridge_track_limit_bug.cpp
    audio/test_audiobridge_plugin_cleanup_bug.cpp
    automation/test_auto_tempo.cpp
    audio/test_audio_clip_stretch.cpp
    clips/test_clip_bpm_invariant.cpp
    audio/test_source_pool.cpp
    clips/test_clip_event_model.cpp
    clips/test_clip_schema_migration.cpp
    audio/test_audio_source_length.cpp
    audio/test_audio_export.cpp
    audio/test_offline_render_contract.cpp
    audio/test_audio_to_midi.cpp
    project/test_command.cpp
    agents/test_command_model.cpp
    agents/test_command_model_downloader.cpp
    agents/test_unigram_tokenizer.cpp
    agents/test_command_model_onnx.cpp
    utilities/test_interfaces.cpp
    midi/test_midi_clip_sync.cpp
    midi/test_midi_clip_split.cpp
    midi/test_midi_legato.cpp
    midi/test_midi_offset.cpp
    clips/test_pianoroll_note_position.cpp
    devices/test_nested_racks.cpp
    devices/test_rack_audio.cpp
    automation/test_modulation.cpp
    controllers/test_control_target.cpp
    automation/test_tempo_sequence_ripple_math.cpp
    devices/test_chain_fingerprint.cpp
    devices/test_chain_info_copy.cpp
    devices/test_drum_grid_pad_commands.cpp
    devices/test_pad_path_types.cpp
    devices/test_structural_undo_roundtrip.cpp
    devices/test_chain_walk.cpp
    devices/test_structural_matrix.cpp
    devices/test_drum_grid_pads.cpp
    devices/test_drum_grid_pads_corpus.cpp
    devices/test_chain_routing_model.cpp
    devices/test_post_fx_chain.cpp
    routing/test_group_tracks_and_macros.cpp
    utilities/test_parameter_utils.cpp
    agents/test_openai_url.cpp
    agents/test_llm_tool_calls.cpp
    project/test_update_checker.cpp
    devices/test_plugin_loading.cpp
    devices/test_plugin_format.cpp
    devices/test_device_authored_state.cpp
    devices/test_sampler_model_edits.cpp
    devices/test_device_state_hydration.cpp
    devices/test_internal_plugin_registry.cpp
    # Engine-neutral internal device state schema v2 (issue #1887)
    devices/test_device_state_codec.cpp
    devices/test_device_sdk_handles.cpp
    devices/test_external_insert_registry.cpp
    devices/test_external_insert_enablement.cpp
    devices/test_insert_capture.cpp
    devices/test_plugin_window_manager.cpp
    devices/test_plugin_capabilities.cpp
    devices/test_plugin_preferences.cpp
    devices/test_plugin_browser_metadata_merge.cpp
    devices/test_plugin_metadata_store.cpp
    devices/test_external_plugin_lookup.cpp
    devices/test_prune_missing_plugins.cpp
    devices/test_known_plugin_list_duplicates.cpp
    devices/test_device_parameter_pagination.cpp
    devices/test_device_ui_context.cpp
    ui/test_levels_ui.cpp
    # LevelsUI builds into magda_daw_app, so magda_tests needs its implementation
    # directly - along with FontManager, which its themed button LookAndFeel pulls in.
    ../magda/daw/ui/components/chain/custom_ui/LevelsUI.cpp
    ../magda/daw/ui/themes/FontManager.cpp
    devices/test_chain_node_path_drag.cpp
    # ChainNodePathDrag builds into magda_daw_app, so magda_tests needs it directly.
    ../magda/daw/ui/components/chain/ChainNodePathDrag.cpp
    ui/test_momentary_param_button.cpp
    audio/test_delta_solo.cpp
    utilities/test_hash_combine.cpp
    clips/test_waveform_editor_absolute_mode.cpp
    clips/test_clip_batch_edit.cpp
    clips/test_clip_commands.cpp
    clips/test_clip_crossfades.cpp
    clips/test_clip_occlusion.cpp
    # Ghost clips / link groups (issue #12)
    clips/test_ghost_clips.cpp
    clips/test_clip_resize_operations.cpp
    midi/test_midi_clip_resize_left.cpp
    clips/test_looped_waveform_tile.cpp
    project/test_project_serialization.cpp
    # Real projects and presets saved by released versions (#2079). Model-level:
    # the load, the migrations it runs and the links it repoints need no engine.
    # The v1 -> v2 device state conversion does, and lives in
    # project/test_legacy_corpus_migration_juce.cpp.
    project/test_legacy_corpus.cpp
    devices/test_persisted_enum_pins.cpp
    devices/test_device_param_migrations.cpp
    project/test_project_document_adapters.cpp
    # Unsaved-changes tracking across the undo stack and project boundaries
    project/test_project_dirty_tracking.cpp
    clips/test_session_clip_playback.cpp
    devices/test_sidechain_triggers.cpp
    devices/test_sidechain_device.cpp
    automation/test_curve_snapshot.cpp
    automation/test_curve_editor_preview.cpp
    automation/test_automation_curve_simplifier.cpp
    # Bake modulation to automation (issue #162)
    automation/test_modulation_baker.cpp
    # Clip-based automation lanes: gap-hold + bake flattener (issue #1087)
    automation/test_automation_clip_lanes.cpp
    devices/test_scan_report.cpp
    devices/test_drum_grid_plugin.cpp
    ui/test_velocity_lane_utils.cpp
    utilities/test_grid_constants.cpp
    utilities/test_ui_scale.cpp
    media/test_media_explorer_preview_state.cpp
    ui/test_timeline_loop_activation.cpp
    routing/test_multi_out_tracks.cpp
    audio/test_freeze_track.cpp
    agents/test_dsl_add_chord.cpp
    agents/test_dsl_add_arpeggio.cpp
    agents/test_dsl_track_workflow.cpp
    agents/test_agent_device_catalog.cpp
    agents/test_chord_agent.cpp
    dsp/test_faust_agent.cpp
    # The prompts' worked examples must satisfy the validator (Faust instrument
    # [idx:N] report)
    dsp/test_faust_prompt_examples.cpp
    agents/test_dsl_project_commands.cpp
    agents/test_dsl_rack_commands.cpp
    agents/test_scale_detection.cpp
    automation/test_step_clock.cpp
    # The sequencing core's rules - rests, ties, glides, gates, probability -
    # away from any device (#2313)
    midi/test_step_sequencer_core.cpp
    # A step pattern in the model's device state document (#2313)
    devices/test_step_pattern_state.cpp
    # Coalescing a pattern drag into one undo step, and only that drag (#2335)
    devices/test_step_pattern_commands.cpp
    # Handing a published pattern to the audio thread a block at a time (#2335)
    midi/test_published_pattern.cpp
    utilities/test_string_table.cpp
    # Guards the juce::String(const char*) ASCII-decode trap (#1858 follow-up)
    utilities/test_non_ascii_string_literals.cpp
    # Arrangement pointer hit tester (#1719)
    ui/test_arrangement_hit_tester.cpp
    # Curve-view readout stays inside the plot (#2072)
    automation/test_curve_label_layout.cpp
    # Transport sections measure themselves instead of estimating (#2071/#2074).
    # TransportLayout builds into magda_daw_app, so pull its implementation in.
    ui/test_transport_layout.cpp
    ../magda/daw/ui/panels/TransportLayout.cpp
    # Panel tab layout survives a restart (#2103). The panel sources live in
    # magda_daw_app rather than the magda_daw library, so pull them in directly.
    ui/test_panel_tab_persistence.cpp
    ../magda/daw/ui/panels/state/PanelController.cpp
    ../magda/daw/ui/panels/content/PanelContent.cpp
    # Keep test runs out of the developer's real config.json. The redirect must
    # be linked into every test binary, and the guard test proves it took.
    TestConfigRedirect.cpp
    project/test_config_path_redirect.cpp
    # Keyboard clip nudging (#1957)
    clips/test_clip_nudge.cpp
    # The MPE pitch glide's curve (#2198): the shape the piano roll draws, the
    # compiler compiles and the Tracktion bridge hands over, and the inverse the
    # bend gesture drives it by.
    clips/test_pitch_expression_curve.cpp
    # Where a clip drag may land (#2179): the pure index rules that step the
    # ghost over a lane it cannot land on. The panel driving them is in
    # magda_juce_tests, which is where a TrackContentPanel can exist.
    clips/test_clip_drag_targets.cpp
    clips/test_mono_note_processor.cpp
    routing/test_sends_and_track_deletion.cpp
    # Internal track-to-track routing (issue #1690)
    routing/test_internal_track_routing.cpp
    routing/test_master_track_serialization.cpp
    project/test_utility_width_migration.cpp
    controllers/test_legacy_device_aliases.cpp
    agents/test_automation_agent.cpp
    agents/test_mix_analysis_agent.cpp
    agents/test_mix_analysis_audio.cpp
    routing/test_console_routing.cpp
    agents/test_conversation_store.cpp
    agents/test_agent_runtime.cpp
    agents/test_agent_tool_bridge.cpp
    agents/test_console_agent_orchestrator.cpp
    midi/test_midi_context.cpp
    agents/test_llama_model_manager.cpp
    routing/test_track_reorder.cpp
    # What each track type declares about itself (#2172)
    routing/test_track_type_traits.cpp
    # New tracks take their post-FX fader side from the preference (#2094)
    devices/test_post_fx_fader_default.cpp
    automation/test_automation_modes.cpp
    automation/test_automation_stepped_targets.cpp
    automation/test_automation_singleton.cpp
    agents/test_instruction_executor_context.cpp
    # Faust pool refactor (Phase 1: metadata parser)
    dsp/test_faust_metadata_parser.cpp
    dsp/test_faust_ui_harvester.cpp
    dsp/test_faust_device_layout.cpp
    # Device layouts build into magda_daw_app, so layout tests need their implementations directly.
    ../magda/daw/ui/components/chain/layout/StandardDeviceLayout.cpp
    ../magda/daw/ui/components/chain/layout/FaustDeviceLayout.cpp
    ../magda/daw/ui/components/chain/layout/CompiledFaustDeviceLayout.cpp
    ../magda/daw/ui/components/chain/slot/DeviceSlotParamLayoutFactory.cpp
    dsp/test_faust_param_pool.cpp
    dsp/test_faust_param_info.cpp
    dsp/test_faust_patch_info.cpp
    dsp/test_faust_custom_view.cpp
    # Runtime execution backend (interp / wasm+wasmtime), issue #1382
    dsp/test_faust_backend.cpp
    # Parameter alias system tests (PR 1)
    controllers/test_param_name_normalize.cpp
    controllers/test_alias_parser.cpp
    # Parameter alias system tests (PR 2)
    controllers/test_alias_registry.cpp
    # Parameter alias system tests (PR 3)
    controllers/test_alias_resolver.cpp
    # Track / master level writes driven by control surfaces
    routing/test_track_level_control_writes.cpp
    # Parameter alias system tests (PR 4 -- AutoAliasGenerator)
    controllers/test_alias_auto_gen.cpp
    # Parameter alias system tests (PR 5 -- CuratedAliasLoader)
    controllers/test_curated_alias_loader.cpp
    # Parameter alias system tests (PR 7 -- Automation alias targets)
    automation/test_automation_alias_targets.cpp
    # Controller data model tests (issue #1050 -- Phase A)
    controllers/test_binding_transform.cpp
    # OSC control surfaces (issue #1757)
    controllers/test_osc_address.cpp
    controllers/test_osc_packet.cpp
    controllers/test_osc_router.cpp
    controllers/test_osc_command_sink.cpp
    controllers/test_osc_bindings.cpp
    controllers/test_osc_feedback.cpp
    # Relative transport seek (#1987)
    ui/test_transport_seek.cpp
    # Controller registry tests (issue #1050 -- Phase B)
    controllers/test_controller_registry.cpp
    controllers/test_binding_registry.cpp
    # Controller router tests (issue #1050 -- Phase C)
    controllers/test_controller_router.cpp
    # Gesture router tests (epic:command-registry -- #21)
    controllers/test_gesture_router.cpp
    # Key mapping persistence tests (epic:command-registry -- #20)
    controllers/test_key_mapping_store.cpp
    ui/test_menu_manager.cpp
    # MenuManager builds into magda_daw_app, so magda_tests needs its implementation directly.
    ../magda/daw/ui/windows/MenuManager.cpp
    # MIDI device identifier matcher tests (issue #1050)
    controllers/test_midi_device_match.cpp
    # MIDI Learn session tests (issue #924)
    controllers/test_midi_learn.cpp
    # Controller profile tests (issue #1052)
    controllers/test_controller_profile.cpp
    # Lua runtime tests (issue #29)
    agents/test_lua_runtime.cpp
    # Lua bindings tests (issue #30 — bind MagdaApi)
    agents/test_magda_api_lua_bindings.cpp
    remote/test_remote_api_contract.cpp
    remote/test_remote_permissions.cpp
    remote/test_remote_permission_conformance.cpp
    remote/test_remote_service.cpp
    remote/test_remote_changes.cpp
    remote/test_remote_subscriptions.cpp
    remote/test_remote_model_bridge.cpp
    remote/test_remote_mcp.cpp
    remote/test_remote_mcp_server.cpp
    remote/test_remote_mcp_bridge.cpp
    remote/test_mcp_sse_parser.cpp
    remote/test_remote_websocket_server.cpp
    remote/test_remote_api_host.cpp
    devices/test_device_api.cpp
    devices/test_plugin_parameter_config_store.cpp
    # Rack and chain addressing at depth through the facade (issue #1993)
    remote/test_track_api_nested_racks.cpp
    automation/test_automation_api_surface.cpp
    # Lua controller tests (issue #592 — MIDI dispatch)
    controllers/test_lua_controller.cpp
    # Focused device API tests
    integration/test_focused_api_live.cpp
    # SelectionManager listener reentrancy
    ui/test_selection_manager_listeners.cpp
    # Configurable user data paths
    project/test_app_paths.cpp
    # Mixing pt2 (issue #1388) - per-track loudness/level/stereo measurement DSP
    audio/test_track_measurer.cpp
    # Mixing pt2 (issue #1390) - inter-track frequency masking detector
    audio/test_masking_detector.cpp
    # Mixing pt2 (issue #1390) - per-track band spectrum (FFT bin -> band mapping)
    audio/test_band_spectrum.cpp
    # Media database (issue #768) — Phase A: SQLite skeleton
    media/test_media_database.cpp
    # Media database (issue #768) — Phase B: scanner + path rules
    media/test_media_db_scan.cpp
    media/test_media_db_path_rules.cpp
    # Media database (issue #768) — Phase C: audio features
    media/test_media_db_audio_features.cpp
    # Media database (issue #768) — Phase D: CLAP audio encoder + mel
    media/test_media_db_clap_encoder.cpp
    # Media database (issue #768) — Phase D2: CLAP text encoder
    media/test_media_db_clap_text_encoder.cpp
    # Media database (issue #768) — Phase D3: RoBERTa tokenizer
    media/test_media_db_roberta_tokenizer.cpp
    # Media database (issue #768) — Phase E1: MediaDbIndexer
    media/test_media_db_indexer.cpp
    # Media database (issue #768) — Phase E2: MediaDbQuery (hybrid search)
    media/test_media_db_query.cpp
    # Media database (issue #1319) — CLAP zero-shot tagging
    media/test_media_db_zero_shot_tags.cpp
    audio/test_limiter_dsp.cpp
    # Stem separation (issue #1288) — HPSS DSP engine + Demucs OLA geometry
    audio/test_hpss_separator.cpp
    audio/test_demucs_chunking.cpp
    audio/test_demucs_separator.cpp
    audio/test_spleeter_separator.cpp
    core/test_device_info_predicates.cpp
)

# Native audio engine (epic #1882). The engine is a dark target with no TE and
# no UI, but its compiler is exercised here: the model fixtures it compiles are
# plain value types, so the plan tests are ordinary model-level Catch2 tests.
if(MAGDA_BUILD_NATIVE_ENGINE)
    list(APPEND TEST_SOURCES engine/test_render_plan_compiler.cpp)
    list(APPEND TEST_SOURCES engine/test_render_plan_executor.cpp)
    list(APPEND TEST_SOURCES engine/test_parallel_executor.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_layout.cpp)
    # The note-range gate a pad chain needs (#2200).
    list(APPEND TEST_SOURCES engine/test_plan_note_gate.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_pad_rack.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_insert.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_diff.cpp)
    # Rack nesting, recursion and op-key identity (#2137). Reaches the parameter
    # table as well as the plan, because the nearest-scope rule the two resolve
    # against is what has to agree across a nesting change.
    list(APPEND TEST_SOURCES engine/test_plan_rack_nesting.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_crossfade.cpp)
    # Plan goldens (#2076). Fixtures and runner both reach into magda_engine.
    list(APPEND TEST_SOURCES PlanGoldenFixtures.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_goldens.cpp)
    # Differ property tests over random edit sequences (#2077). The vocabulary
    # and the session the properties publish into are model-only, so they build
    # here beside the runner rather than needing a te::Edit.
    list(APPEND TEST_SOURCES PlanEditSequence.cpp)
    list(APPEND TEST_SOURCES PlanEditHarness.cpp)
    list(APPEND TEST_SOURCES engine/test_plan_edit_properties.cpp)
    list(APPEND TEST_SOURCES engine/test_engine_session.cpp)
    # The parameter lane (#2116). The lanes are fed by hand, so the precedence
    # rules are testable before anything writes them.
    list(APPEND TEST_SOURCES engine/test_param_lane.cpp)
    # The parameter table (#2117): addressing, the link graph and publication.
    list(APPEND TEST_SOURCES engine/test_param_table.cpp)
    # Automation lanes baked into the parameter lane (#2118).
    list(APPEND TEST_SOURCES engine/test_automation_bake.cpp)
    # The LFO engine: shapes, rates, triggers and gates (#2119).
    list(APPEND TEST_SOURCES engine/test_mod_lfo.cpp)
    # The remaining three modifier engines, and the feeds two of them need
    # (#2120): the envelope's gate, the walk, and the follower's source.
    list(APPEND TEST_SOURCES engine/test_mod_adsr.cpp)
    list(APPEND TEST_SOURCES engine/test_mod_random.cpp)
    list(APPEND TEST_SOURCES engine/test_mod_follower.cpp)
    list(APPEND TEST_SOURCES engine/test_mod_feeds.cpp)
    # Macros at track, rack and device scope (#2121): which array a path
    # names, nested racks, and a macro reaching a modifier's rate.
    list(APPEND TEST_SOURCES engine/test_param_macros.cpp)
    # What the UI reads back (#2122): the modifier's output and the parameter's
    # resolved value, published by the block that produced them.
    list(APPEND TEST_SOURCES engine/test_value_taps.cpp)
    list(APPEND TEST_SOURCES engine/test_tempo_map.cpp)
    list(APPEND TEST_SOURCES engine/test_transport_clock.cpp)
    # The two meanings of a sample offset inside a block (#2336): an event on a
    # sample the block plays, an edge that may be the boundary past its last.
    list(APPEND TEST_SOURCES engine/test_block_samples.cpp)
    # The launcher's state machine (#2300): quantized launch and stop against
    # monotonic beats, and the split a block straddling one reports.
    list(APPEND TEST_SOURCES engine/test_launch_handle.cpp)
    # Which slot handles survive a publish (#2301): the plan is per track and a
    # handle is per slot, so the snapshot is what names them.
    list(APPEND TEST_SOURCES engine/test_launch_handle_lifetime.cpp)
    # The lane a launch arrives on (#2305): the block it lands in, the order it
    # keeps, and that it cannot be applied twice or reach a slot that has gone.
    list(APPEND TEST_SOURCES engine/test_launch_requests.cpp)
    # What a slot publishes about itself (#2303): the block's own reading,
    # rather than a UI polling the launcher a frame behind the audio.
    list(APPEND TEST_SOURCES engine/test_launch_tap.cpp)
    # What ends a slot's run and what starts next (#2304), and that a launch
    # the engine made reads out exactly as a clicked one does.
    list(APPEND TEST_SOURCES engine/test_follow_actions.cpp)
    list(APPEND TEST_SOURCES audio/test_click_generator.cpp)
    list(APPEND TEST_SOURCES audio/test_prefetch_stream.cpp)
    list(APPEND TEST_SOURCES audio/test_source_readers.cpp)
    list(APPEND TEST_SOURCES audio/test_streaming_audio_source.cpp)
    list(APPEND TEST_SOURCES audio/test_file_audio_source.cpp)
    list(APPEND TEST_SOURCES engine/test_offline_render.cpp)
    list(APPEND TEST_SOURCES engine/test_engine_taps.cpp)
    list(APPEND TEST_SOURCES engine/test_clip_snapshot.cpp)
    list(APPEND TEST_SOURCES engine/test_clip_voice.cpp)
    # The two edges of a voice (#2302): the step a start subtracts and the
    # one a stop carries down, which is what a section switch is made of.
    list(APPEND TEST_SOURCES engine/test_de_click.cpp)
    list(APPEND TEST_SOURCES engine/test_session_playback.cpp)
    list(APPEND TEST_SOURCES engine/test_clip_voice_pool.cpp)
    list(APPEND TEST_SOURCES engine/test_clip_stretch.cpp)
    list(APPEND TEST_SOURCES engine/test_clip_warp.cpp)
    list(APPEND TEST_SOURCES midi/test_midi_clip_playback.cpp)
    list(APPEND TEST_SOURCES audio/test_groove_template.cpp)
    list(APPEND TEST_SOURCES audio/test_transient_detector.cpp)
    list(APPEND TEST_SOURCES audio/test_source_loop_info.cpp)

    # The null-diff corpus (#2040). The comparator and the material it plays are
    # model-only and live here, where they can be tested against known-bad pairs
    # on their own; the two legs and the runner need a te::Edit and so live in
    # magda_juce_tests.
    list(APPEND TEST_SOURCES NullDiffMaterial.cpp)
    list(APPEND TEST_SOURCES NullDiffCompare.cpp)
    list(APPEND TEST_SOURCES NullDiffCorpus.cpp)
    list(APPEND TEST_SOURCES NullDiffHostedPlugin.cpp)
    list(APPEND TEST_SOURCES NullDiffNativeLeg.cpp)
    list(APPEND TEST_SOURCES engine/test_null_diff_compare.cpp)
    list(APPEND TEST_SOURCES engine/test_null_diff_corpus.cpp)
    list(APPEND TEST_SOURCES engine/test_null_diff_native_leg.cpp)

    # The .mgd fixture rig (#2173): a real project turned into a case. Model
    # only, like the corpus beside it -- the load produces model values and this
    # slice renders nothing, so neither leg is involved.
    list(APPEND TEST_SOURCES MgdFixture.cpp)
    list(APPEND TEST_SOURCES MgdFixtures.cpp)
    list(APPEND TEST_SOURCES engine/test_mgd_fixture_rig.cpp)

    # What the legacy projects would cost as render cases (#2081). A survey
    # rather than a corpus: it declares no tier and asserts no residual, and
    # reports what stands between each project and being a case.
    list(APPEND TEST_SOURCES engine/test_real_project_survey.cpp)

    # The default Faust synth patch, compiled and played through libfaust
    # (#2237). Here rather than in magda_juce_tests because the plugin that
    # hosts it wants an Edit, and that target cannot have the Faust standard
    # library staged beside it while #2238 stands.
    list(APPEND TEST_SOURCES dsp/test_faust_default_synth_patch.cpp)

    # The device layer under the engine (#2174): the app's own catalogs asked
    # for a Device op's device, and a shipped device rendered through the
    # adapter. The engine's half alone -- what the two hosts do with one device
    # is the corpus's question, in magda_juce_tests.
    list(APPEND TEST_SOURCES engine/test_engine_device_layer.cpp)

    # An external plugin under a Device op (#2241): the adapter over
    # juce::AudioPluginInstance, against a plugin written in the test rather
    # than one that happens to be installed. What a real plugin is for is the
    # corpus (#2175), where both engines run the same one.
    list(APPEND TEST_SOURCES engine/test_engine_external_device.cpp)

    # Who an asynchronous plugin load belongs to (#2261): the runtime-owned
    # assignment the adapter above checks before it completes one.
    list(APPEND TEST_SOURCES engine/test_plugin_assignments.cpp)
    list(APPEND TEST_SOURCES engine/test_control_executor.cpp)

    # The block-size invariance gate (#2078). Model-only for the same reason the
    # native leg is: the claim is the native engine's alone, and the incumbent
    # owes nobody block-size invariance. In CI rather than in a nightly, because
    # a gate that runs after the merge is a report rather than a gate.
    list(APPEND TEST_SOURCES engine/test_null_diff_block_size.cpp)

    # The DAWproject cross-check (#2080). Native against native: each corpus
    # project is exported, reimported, and required to compile to the same plan
    # and render to the same samples. Model-only because both sides are the
    # native engine and the export path is plain serialization.
    list(APPEND TEST_SOURCES DawProjectRoundTrip.cpp)
    list(APPEND TEST_SOURCES engine/test_dawproject_round_trip.cpp)
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
    # Faust runtime headers + the backend selection, so dsp/test_faust_backend.cpp
    # resolves to the same factory the app was built with.
    magda_faust_backend
    # cpp-httplib ships both an HTTP and a WebSocket client, so the transport
    # tests in remote/test_remote_websocket_server.cpp and remote/test_remote_mcp_server.cpp
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
    # Test sources live in category subdirectories; shared fixtures/mocks stay
    # at tests/ root, so quote-includes of them need this on the search path.
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    # TempoLaneSync is tested directly in automation/test_tempo_lane_sync_juce.cpp instead.
    MAGDA_NO_AUTO_TEMPO_LANE_SYNC=1
    # Friend access for agents/test_llama_model_manager.cpp. The define also happens to
    # arrive transitively via magda_daw's INTERFACE re-export of its own
    # COMPILE_DEFINITIONS; set it explicitly so this target does not depend on
    # that re-export staying broad.
    MAGDA_ENABLE_TEST_HOOKS=1
    # remote/test_remote_mcp_bridge.cpp forks the real bridge binary: the thing it
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
    # Where the bundled runtime patches and the Faust standard library are, for
    # the test that compiles the default synth (#2237). Read from the source
    # tree rather than from the staged copy beside the binary: the patch is what
    # is under test, and a test that read the staged copy would pass on a stale
    # one.
    MAGDA_FAUST_RUNTIME_DSP_DIR="${CMAKE_SOURCE_DIR}/magda/daw/audio/faust_dsp/runtime"
    MAGDA_FAUST_LIBRARIES_DIR="${CMAKE_SOURCE_DIR}/third_party/faust/libraries"
)

# The Faust standard library, beside the binary, so a runtime-compiled device
# can resolve import("stdfaust.lib") (#2081). Same reason as magda_juce_tests:
# a real project carries a Faust device whose saved source imports it, and a
# device whose DSP will not compile passes audio through without saying so.
magda_stage_faust_libraries(magda_tests)

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
    utilities/test_zoom_simple.cpp
    ui/test_export_transport_stop_juce.cpp
    engine/test_offline_render_device_power_juce.cpp
    engine/test_tracktion_engine_wrapper_refactoring_juce.cpp
    # engine/test_export_no_assertion_juce.cpp  # Disabled - see issue #611 (generator device export)
    engine/test_assertion_watch_juce.cpp
    midi/test_midi_recording_juce.cpp
    clips/test_clip_sync_integration_juce.cpp
    ui/test_signalsmith_loop_juce.cpp
    ui/test_arranger_launcher_switching_juce.cpp
    ui/test_node_id_collision_juce.cpp
    ui/test_transport_seek_juce.cpp
    midi/test_session_slot_recording_juce.cpp
    routing/test_input_monitor_track_routing_juce.cpp
    # Hardware output selections must reach the TE wave output devices (#2264)
    routing/test_hardware_output_routing_juce.cpp
    ../magda/daw/ui/components/mixer/RoutingSelector.cpp
    controllers/test_controller_track_level_write_juce.cpp
    # OSC control surfaces (issue #1757): a live receive thread and real packets
    controllers/test_osc_service_juce.cpp
    devices/test_post_fx_fader_order_juce.cpp
    midi/test_track_midi_recording_preview_juce.cpp
    ui/test_timeline_loop_activation_juce.cpp
    midi/test_midi_signal_routing_juce.cpp
    devices/test_drum_grid_serialization_juce.cpp
    devices/test_sampler_relocate_juce.cpp
    devices/test_device_services_juce.cpp
    devices/test_insert_config_bridge_juce.cpp
    devices/test_device_param_schema_juce.cpp
    # pluginState v1 -> v2 over the legacy corpus (#2079). Here rather than in
    # magda_tests because the conversion reads the values off a live plugin.
    project/test_legacy_corpus_migration_juce.cpp
    devices/test_device_state_schema_juce.cpp
    devices/test_convolution_device_juce.cpp
    devices/test_mutable_add_not_replace_juce.cpp
    devices/test_master_sidechain_juce.cpp
    midi/test_midi_bridge_note_events_juce.cpp
    devices/test_section_scoped_device_ids_juce.cpp
    integration/test_remote_service_live_juce.cpp
    # Path-addressed racks/chains against the real TrackManager (issue #1993)
    integration/test_track_api_nested_racks_live_juce.cpp
    remote/test_remote_websocket_live_juce.cpp
    clips/test_clip_inspector_juce.cpp
    clips/test_multi_clip_resize_preview_juce.cpp
    clips/test_clip_paint_juce.cpp
    clips/test_clip_nudge_juce.cpp
    clips/test_clip_drag_targets_juce.cpp
    # A .mid dropped on the chord track arrives as chords
    ui/test_chord_track_file_drop_juce.cpp
    # Files dropped straight onto a Drum Grid track (#2172)
    ui/test_drum_grid_file_drop_juce.cpp
    ui/test_multiband_curve_view_juce.cpp
    # The transport layout against the widths the shipped fonts really produce
    # (#2071). Needs a graphics context, so it lives here rather than in
    # magda_tests, which asserts the same layout as pure arithmetic.
    ui/test_transport_layout_juce.cpp
    ../magda/daw/ui/panels/TransportLayout.cpp
    ../magda/daw/ui/panels/TransportTextWidths.cpp
    # Keep test runs out of the developer's real config.json (see the copy in
    # magda_tests): RemoteApiHost persists grants via Config::save(). The guard
    # test is duplicated per target because each uses its own framework.
    TestConfigRedirect.cpp
    project/test_config_path_redirect_juce.cpp
    # Boolean param cells follow value-only refreshes (#2072 follow-up).
    # ParamSlotComponent builds into magda_daw_app, so the test target needs it
    # and the param helpers it calls into compiled in directly.
    ui/test_param_slot_boolean_sync_juce.cpp
    # A discrete cell's dropdown must not write the parameter on rebuild, and
    # must map an external plugin's TE-native value onto its choices.
    ui/test_param_slot_discrete_combo_juce.cpp
    ../magda/daw/ui/components/chain/params/ParamSlotComponent.cpp
    ../magda/daw/ui/components/chain/params/ParamWidgetSetup.cpp
    ../magda/daw/ui/components/chain/params/ParamLinkResolver.cpp
    ../magda/daw/ui/components/chain/params/ParamLinkMenu.cpp
    ../magda/daw/ui/components/chain/params/ModulationBakeAction.cpp
    ../magda/daw/ui/components/chain/params/ParamModulationPainter.cpp
    ui/test_timeline_extreme_zoom_paint_juce.cpp
    ui/test_repaint_invariance_juce.cpp
    ui/test_arrange_beat_native_juce.cpp
    automation/test_tempo_map_juce.cpp
    automation/test_tempo_lane_bridge_juce.cpp
    automation/test_tempo_lane_sync_juce.cpp
    dsp/test_faust_sidechain_juce.cpp
    dsp/test_faust_instrument_tempo_juce.cpp
    dsp/test_faust_instrument_voice_modes_juce.cpp
    ui/test_runtime_theme_juce.cpp
    ui/test_user_theme_juce.cpp
    ui/test_syntax_highlighting_juce.cpp
    ../magda/daw/ui/code/SyntaxTheme.cpp
    ../magda/daw/ui/code/DSLTokeniser.cpp
    ../magda/daw/ui/code/FaustTokeniser.cpp
    devices/test_device_gain_slider.cpp
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
    ui/test_arrangement_row_alignment_juce.cpp
    automation/test_automation_modes_juce.cpp
    ui/test_arrangement_transport_loop_juce.cpp
    devices/test_external_plugin_state_restore_juce.cpp
    agents/test_chord_progression_converter_juce.cpp
    ui/test_audio_driver_type_selection_juce.cpp
    automation/test_tempo_sequence_ripple_juce.cpp
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
        NullDiffHostedPlugin.cpp
        NullDiffNativeLeg.cpp
        NullDiffTeLeg.cpp
        NullDiffRunner.cpp
        engine/test_null_diff_juce.cpp

        # The real-project corpus (#2081). The fixture rig is compiled into
        # magda_tests too, where it is tested without rendering anything; here
        # it feeds the same runner the code-built corpus uses, so the two
        # corpora cannot disagree about what counts as a null.
        MgdFixture.cpp
        MgdFixtures.cpp
        engine/test_real_project_corpus_juce.cpp
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
    # facade in remote/test_remote_websocket_live_juce.cpp. magda_daw links it PRIVATE.
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

# The Faust standard library is deliberately NOT staged beside this binary,
# unlike magda_tests (#2081).
#
# It was, and it aborts the suite on Linux. This target instantiates every
# registered device -- the device parameter schema freeze walks the whole
# registry -- and that includes the Faust instrument. With the library present
# its patch actually compiles, which reaches libfaust's interval analysis, which
# asserts `x.lo() > 0` in fPow and takes the process down with it
# (third_party/faust/compiler/interval/intervalPow.cpp:89). GCC hits it and
# Clang does not, so it is Linux only and invisible on a Mac.
#
# The patch's own divide by zero (#2237) looked like the cause and was not.
# Fixing it did not clear the assert, and could not have: the power the analysis
# refuses is `csq = c*c` inside Faust's own tf2s, which every tf2s-based filter
# carries. A patch using nothing but `fi.resonlp` with a constant Q generates it
# just the same, so this waits on #2238 rather than on anything of ours.
#
# What the corpus loses is the one project hosting a runtime Faust device, named
# and skipped where the skip is (engine/test_real_project_corpus_juce.cpp). magda_tests
# keeps the staging: it renders that same project through the block-size gate,
# instantiates no Faust instrument, and is green on Linux and macOS.

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
    MAGDA_LEGACY_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/legacy"
    # The root the .mgd fixtures name a file under (#2173), for the real-project
    # corpus that renders them through both legs (#2081).
    MAGDA_TEST_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")

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
    # Test sources live in category subdirectories; shared fixtures/mocks stay
    # at tests/ root, so quote-includes of them need this on the search path.
    ${CMAKE_CURRENT_SOURCE_DIR}
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
