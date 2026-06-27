# Handoff: epic/native-instrument-ui-polish (issue #1615)

Branch: `epic/native-instrument-ui-polish` (off `dev/0.13.0`). Build local with `make debug`.

## Done (committed)
- **Stuck-notes hardening** — Poly Synth, FM0, drum voices (shared `MagdaCompiledPolyInstrument`) dedup note-ons per pitch; Elements honors CC120/123. Rings/Clouds N/A.
- **#1614 Poly Synth UI** — four oscillator columns, icon wave selectors (`IconSelector`), per-osc enable toggle as column header + grey-out, phase-reset "R", panel widened to 860. New `Osc N Enable` DSP slots (idx 39-42, default On). Cosmetic follow-up: full-width voice mode, left-aligned smaller retrigger toggle.
- **FM0 per-operator enable** — `Op N Enable` slots (idx 38-41, default On), gated before the feedback bus, column grey-out.
- **#1616 ADSR graphs** — draggable `AdsrGraph` wired into 4OSC Amp + Filter tabs, both Mod Env envelopes; LFO tab gets static one-cycle wave-shape previews. Sampler envelope overlay polished (brighter/thicker blue line, bigger handle dots).
- **Crash fix** — dangling `[&]` capture in `DeviceSlotParameterPaging.cpp` (param-slot drag → SIGSEGV); now captures the two function-locals by value.

## OPEN BUGS (must fix before epic is done)

### 1. FM0 phantom/stuck voice  ← in progress, NOT fixed
Symptom (user): FM0 "emits sound from nowhere" — a voice's gate stays high with no note held. Reproduces after **pressing Stop mid-note** (and seen again around project load). Output is `* env` (gate-driven), so sound-from-nowhere == a note-on whose note-off never arrived.
- Most likely: no note-off / all-notes-off delivered to the instrument on transport **stop**, leaving the Faust voice gated on (the graph keeps running for live monitoring, so it keeps sounding).
- Next step was to check `te::PluginRenderContext` for a playing/stopped flag in `MagdaCompiledPolyInstrument::applyToBuffer` and flush voices (`resetAllVoices()` / CC123) on the playing→stopped edge. Confirm with the user whether it's purely the Stop case before fixing. Could be pre-existing (not caused by the dedup change) — verify it didn't regress.
- Diagnostics: a sampler/crash log is the WRONG tool (it's audio, not a crash). Use audio-safe atomics (note-on/off balance) drained by a message-thread timer if needed.

### 2. Osc/Op Enable loads as Off in old projects  ← root-cause not nailed
Symptom: loading a project saved before the enable params existed shows oscillators disabled (silent until toggled on). New projects fine.
- `CachedValue::referTo(state, id, um, defaultNormalized=1.0)` should default a missing enable property to On, and `realToNormalized(1.0)` for the discrete Off/On param correctly gives 1.0 — so the simple theory doesn't hold; something overwrites to 0 on load.
- Suspect path: `restoreDeviceStateWithChunkOverlay` → `DeviceProcessor::syncFromDeviceInfo` (`base/DeviceProcessor.cpp:88`) iterates the saved `DeviceInfo.parameters`; an old project has 39 entries so slots 39-42 aren't written — that alone would LEAVE them at default On, so the 0 comes from elsewhere (whether the loaded DeviceInfo is extended to 43 with currentValue=0, then applied). Need to confirm with logging.
- Diagnostic logging was added to `PluginManagerSync.cpp` (dump enable-slot norm/disp before-restore / after-restore / after-populate + whether the saved DeviceInfo carried enable params) then **reverted** (suspected of causing instability; unconfirmed). Re-add if needed. NOTE: user's `magda.log` is NOT at the default `~/Library/Application Support/MAGDA/Logs/` — ask for the data-dir path.
- Marked by user as "not a massive deal" (old projects only).

## Remaining epic work items (untouched)
- **#1492** — Strum UI: real drawable strum curve (`StepClock::applyRampCurve` + curve editor) replacing the shape presets. In the Strum tab of the compiled-instrument tabbed UI.
- **#1120** — Generic AI device-preset agent driven by text-only per-device metadata (bundled 4-OSC bootstrap, user-override path, "Edit Device Metadata..." menu item). Largest item.
- **#1489** — Restore Mallet instrument, percussion-focused (struck/modal pm.lib models). Extract chord-latch + curve strum scheduler + limiter from `MagdaPluckCompiledPlugin` into a shared base. Open Q: include fixed-pitch bells or pitched-mallets only.
- **DrumGrid** — bug: can't load the new instruments into pads (and check racks). Plus TBD improvements.

## Notes
- Per-osc/op enable params are **name-keyed** (`prefix + slot.name`), so appending them preserves saved Gain/Voice Mode state.
- Compiled-Faust host-slot contract: `.dsp` `[idx:N]` ↔ wrapper `kHostSlotCount`/slot constants ↔ wrapper `hostSlotInfo_` ↔ UI `kNumParams`. Append, never renumber. Validate `.dsp` with the `faust-mcp-magda` MCP `compile_faust` before building (`enable` is a reserved Faust keyword — FM uses `opEn`).
