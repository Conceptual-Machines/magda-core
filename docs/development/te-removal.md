# Tracktion Engine removal

Tracking: #2557, #2918–#2921. Baseline: `dev/1.0.0` at `0d92b0e30`.
JUCE 9 (#2922) follows a qualified native-only build. Project version control
(#2251) follows those platform changes.

## First implementation: saved device state

`audio/plugins/SavedDeviceState` now owns the shared restore reader. It accepts
current device JSON and legacy XML, preserves device properties, binary data
and ordered children, resolves registered load aliases, and refuses unsupported
future documents. The model, authored-state projection, 4OSC translation and
both engine adapters use that reader. Track duplication and device presets
clean old object IDs/modifier assignments through `DeviceState` rather than TE
helpers. The Tracktion bridge retains only live-TE capture and parameter writes.

The state-reader tests can be built with JUCE and Catch2 alone: compile
`DeviceState.cpp`, `InternalPluginRegistry.cpp`, `SavedDeviceState.cpp` and
`tests/devices/test_saved_device_state.cpp`. No TE headers, target or library
are required. The same tests are registered in the normal Catch2 suite and in the standalone
`magda_saved_state_tests` target (`ctest -R saved_device_state_native`).

## Remaining production consumers

These are dependency groups observed in the baseline source/build files. A
native counterpart existing is not evidence that every user workflow has been
verified. Record that verification before deleting its legacy implementation.

| Consumer | Native path / removal work | Tracking |
| --- | --- | --- |
| Root CMake, `.gitmodules`, `MAGDA_JUCE_STACK` | Remove unconditional TE subdirectory/link and submodule after consumers are gone; explicitly list any JUCE modules TE used to supply | #2921 |
| `magda_tracktion_compat_devices`, compiled-device Tracktion adapters | Keep engine-neutral `magda_devices`, `magda_base_devices` and `magda_engine_devices`; audit legacy pack-only registrations/assets before deleting | #2918 |
| `TracktionEngineWrapper*`, `TracktionAudioIO`, `MagdaUIBehaviour`, `MagdaEngineBehaviour` | `MagdaAudioEngine`, `engine/host/EngineHost`, `AudioIOService` and application-owned services; delete incumbent wrapper lifecycle | #2918 |
| `AudioEngineChoice`, engine factory in `TracktionEngineWrapperInit`, Preferences engine picker | One native construction path; migrate old preferences and define retired environment-override behavior | #2919, #2921 |
| `AudioBridge`, `TrackController`, `AudioBridgeMixer` | Native model compiler, host project synchronization, native mixer/parameter operations | #2918 |
| `ClipSynchronizer`, `ClipWarpSynchronizer`, `WarpMarkerManager`, arrangement sync planner | Native clip compiler/read/stretch path; retain independent timeline/warp regression expectations | #2918, #2920 |
| `SessionClipScheduler`, `SessionRecorder`, `SessionMonitorPlugin`, incumbent clip quantization | Host `SlotLauncher`, live input/recording and session-arrangement capture; verify slot stop/cancel and capture behavior | #2918, #2920 |
| `TempoLaneBridge`, `TempoLaneSync`, `TempoSequenceRippleCommand`, `TracktionTempoMap` | Native tempo map and project model; port any remaining edit-scoped listeners and undo behavior | #2918 |
| Plugin manager sync/macros/modifiers/sidechain/measurement, rack sync, instrument rack manager | Native device factory, parameter/control executor, plan compiler and host plugin assignments; delete TE object mirroring | #2918 |
| TE `DeviceProcessor` hierarchy and parameter builder | Native device parameter descriptions and model edits; retain device metadata/schema tests | #2918, #2920 |
| `PluginWindowManager`, `PluginWindowBridge` | Native per-device editors via `EngineExternalDevice` and `AudioEngine` editor methods; verify close/remove/reload lifetime | #2918 |
| `ExternalInsertDeviceEnablement`, insert capture service/plugins and `InsertConfigBridge` | Native host insert routing/capture and hardware catalog; verify port naming, enabling, bypass and latency | #2918 |
| Drum Grid plugin and pad UI bindings | Native pad-chain model compilation, rendered-device handles and host meter taps; remove TE plugin pointers from UI contracts | #2918 |
| `TracktionFork` and custom UI 4OSC calls | Existing 4OSC-to-Poly-Synth translation handles old projects; verify unsupported controls are reported before retiring live 4OSC editing | #2918, #2919 |
| Transient detection calls in waveform/editor/inspector views | Identify/verify native analysis provider; null-returning `TracktionFork` helpers are a remaining behavior gap | #2918 |
| `AutomationManager` base-value lookup; TE automation bake/playback/recording and modifier helpers | Native parameter lanes, model automation and modulation; preserve touch/write/bake behavior and undo | #2918, #2920 |
| `ClipCommands` legacy sampler/pad extraction | Native/model extraction path; verify slicing to sampler/Drum Grid and retain media references | #2918, #2919 |
| `transport_api_live` edit listener / beat-to-time fallback | Native tempo-map and transport callbacks already supplied by `MagdaAudioEngine`; remove TE fallback/listener ownership | #2918 |
| `plugin_api_live` 4OSC state editing | Canonical native device state/parameter path and explicit legacy migration diagnostics | #2918, #2919 |
| TE measurement/follower/sidechain/MIDI receive/meter-tap plugins | Native plan taps, buses, analysis and MIDI routing; audit registration needs before deleting compatibility devices | #2918 |
| `EngineEnumPins`, persisted stretch/fade/modulation values | Keep MAGDA's persisted integers; delete comparisons to the retired engine, retain native interpretation fixtures | #2919, #2920 |
| `TracktionAudioSettings` | Already a JUCE/application-owned legacy settings reader; retain migration behavior and rename independently of removing the runtime | #2919 |
| `GrooveStore` default resources, icon assets and dependency notices | Confirm retained bundled data has independent provenance; remove only unused fork payload | #2918, #2921 |
| Profiling `BenchmarkSuite` TE engine signature | Replace with native/application profiling or retire unused benchmark callers | #2918 |

The saved-state changes remove a dependency, not the whole legacy adapter. In
particular `BaseDevicePack`, plugin sync and the wrapper still have live TE
capture/construction calls, while model readers no longer require them.

## Qualification and deletion order

1. Finish native release qualification under #2557: existing parity validation,
   user-bug regression pass (#2641), performance tracking and installed-app smoke
   work (#2781/#2782/#2784).
2. Complete runtime decoupling (#2918) and project/settings migration (#2919).
3. Preserve native-only regression coverage (#2920) before deleting the incumbent
   oracle. Keep plan goldens, reference/parallel consistency, block-size
   invariance, DAWproject round trips and installed-app smoke projects.
4. Delete the fork, legacy runtime branches, build links/options, CI and packaging
   dependencies (#2921). Validate Debug/Release builds and installed artifacts on
   supported macOS, Windows and Linux configurations from a fresh checkout.
5. Upgrade the exact pinned JUCE revision in a separate change (#2922).

## Audit commands

```sh
rg -n 'tracktion_engine|tracktion::|TracktionFork|TracktionEngineWrapper' magda
rg -n 'tracktion|TRACKTION' CMakeLists.txt .gitmodules magda/*/CMakeLists.txt tests/CMakeLists.txt .github scripts
```

Comments, historical docs and legacy fixture spellings can remain after runtime
removal. Build/include/link dependencies and executable fallback branches cannot.
