# Faust integration — free-tier POC notes

End state of `feat/faust-poc`. Replaces the Stage 1 / Stage 2 plan docs.

## What ships

- libfaust 2.85.5 vendored as a submodule at `third_party/faust` (fork: `Conceptual-Machines/faust`), built from source as a static lib via `INTERP_COMP_BACKEND=STATIC`. No LLVM, no brew dep, no `faust` CLI on the build host. CMake cache var `MAGDA_FAUST_BACKEND` (`interp` default; `llvm` wired but unbuilt — pro tier).
- `FaustPlugin` compiles `.dsp` source at runtime via `createInterpreterDSPFactoryFromString`, harvests sliders into `AutomatableParameter`s, persists source in the plugin's `ValueTree` so projects round-trip.
- Hot-swap: `loadDspSource` atomically replaces the active DSP via `std::atomic_load`/`store` on a `shared_ptr<FaustState>`. Slider values are preserved across swaps in an in-memory `{source → {param id → value}}` cache, so `Drive → Tremolo → Drive` restores the user's Drive tweaks.
- Audio-thread destruction guard: a 100 ms `juce::Timer` drains retired `FaustState` snapshots on the message thread once they're ≥200 ms old — far longer than any audio buffer, so the dtor never runs on the audio thread under normal swap timing.
- Bundled resources: `magda_drive.dsp` and `magda_tremolo.dsp` ship via `juce_add_binary_data`; `third_party/faust/libraries` is copied into the bundle (`Contents/Resources/faustlibraries` on macOS, exe-adjacent elsewhere). `FaustPlugin::compile` passes that dir as `-I` so `import("stdfaust.lib")` resolves at runtime.
- UI (`FaustUI`): chain-slot controls show DSP name + Load/Edit buttons. Load opens a popup with bundled starters and "From file…" file picker. Edit opens a top-level code editor window (`FaustCodeEditorWindow`) with a Compile button. Compile errors render inline (red label in the slot, status line in the editor) — no alert dialogs.

## Constructor fallback

If a saved DSP source no longer compiles (libraries moved, syntax change, no bundle path available e.g. tests), `FaustPlugin` falls back to a built-in stereo passthrough so the plugin always loads with no params and audio passing through.

## Known POC limits

- libfaust's compile API is not thread-safe (`startMTDSPFactories`/`stopMTDSPFactories` not used). Loading several Faust plugins simultaneously on background threads would race; not a current scenario.
- No MIDI input / polyphony — effects only. Faust's `[midi:on]` metadata and `mydsp_poly` wrapper aren't routed.
- Slider value preservation across swaps is in-memory only; not persisted to project state.
- `ParamHarvestingUI` ignores buttons / checkboxes / bargraphs / soundfiles. Only sliders show up in the UI.
- Slugified param IDs come from slider labels — renaming a slider in `.dsp` resets that param to its default on next load.
- DSP swap on a running transport will glitch one buffer (atomic swap mid-block).

## Not in this POC, candidate Stage 3

- LLVM JIT backend wiring + smoke test (interp is fast enough in practice — see memory).
- MIDI / polyphony for Faust synths.
- Faust CodeTokeniser for syntax highlighting in the editor (currently plain text).
- Hot-reload on disk file change.
- `startMTDSPFactories` wrap if/when concurrent loads become a thing.
- Deeper UI types (bargraphs as meters, buttons as triggers, grouped boxes).
