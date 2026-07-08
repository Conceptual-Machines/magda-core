# External FX / External Instrument — Handoff

Branch: `feat/external-instrument-fx` (rebased onto `dev/0.15.0`). Needs testing
on a machine with a real audio interface (+ ideally a hardware FX unit and/or a
MIDI synth). Full design is in `EXTERNAL_INSERT_PLAN.md`. Issue: #1623.

## What's implemented

- **Device kind** `InternalDeviceKind::ExternalInsert`, backed by TE's
  `te::InsertPlugin` (`xmlTypeName "insert"`). Created/persisted via the existing
  internal-plugin path; latency auto-compensated by TE's PDC.
- **Browser**: two entries over the one kind —
  - **External FX** (`isInstrument=false`): audio send + audio return.
  - **External Instrument** (`isInstrument=true`): MIDI send + audio return.
- **Slot UI** (`ExternalInsertUI`): a send picker (audio-out for FX, MIDI-out for
  instrument), an audio-return picker, a manual-latency (ms) field, and a
  **Freeze** button with a status line (fourth row of the slot body — make the
  device slot expanded/tall enough to see it). Selections write straight to the
  live plugin's `outputDevice`/`inputDevice`/`manualAdjustMs` +
  `updateDeviceTypes()`.
- **Routing mirror**: track-level MIDI-out / audio-in go read-only and mirror the
  device's selection while an External Instrument is on the track
  (`TrackManager::getExternalInstrumentRouting`, TrackInspector +
  TrackHeadersPanel).
- **Hardware port auto-enable (Phase 3, #1623)**:
  `ExternalInsertDeviceEnablement` (owned by AudioBridge) derives enablement
  from the inserts — a selected/restored send or return port is enabled
  automatically, and ports it auto-enabled are disabled again when no insert
  uses them. Ports the user enabled in Audio Settings are never touched. The
  pickers list disabled ports too (picking one enables it). Triggers:
  devicePropertyChanged, trackDevicesChanged, tracksChanged (post-load).
- **MIDI feedback guard (#1623)**: `MidiInputRouter` drops the send target's own
  input port from the track's MIDI routing (matched by port name, both "All
  Inputs" and explicit selection), so the synth cannot loop its own notes back
  through the insert send. Re-applied when the send target changes.
- **Feedback-port warning (#1623)**: the slot status line warns when another
  enabled insert uses the same send or return port, or the return equals the
  send.
- **Freeze-to-audio (#1623 main item)**: offline export cannot capture outboard
  gear, so Freeze runs a real-time pass — plays the track's clip range through
  the live engine, records the insert's PDC-aligned audio return via a hidden
  `InsertCapturePlugin` placed after the insert, then replaces the track's
  clips with the captured clip, bypasses the insert + everything before it in
  the chain, and stores the whole prior state on the device
  (`DeviceInfo::externalFreeze`, persisted). Unfreeze restores exactly.
  Orchestrated by `InsertFreezeService` (owned by TracktionEngineWrapper,
  reachable via `AudioEngine::getInsertFreezeService`). Export shows a warning
  when an enabled, un-frozen external insert exists (the track would be silent).

Build + `magda_tests` (incl. `test_external_insert_registry.cpp`,
`test_insert_freeze.cpp`) are green. **Runtime behaviour is compile/test
verified only — the hardware paths need a real interface.**

## How to test

1. `make debug && make run` (or launch the built MAGDA.app).
2. Add **External FX** to an audio track from the device browser. Open its slot.
   - Confirm **Send to** lists your interface's audio outputs (including
     disabled ones) and **Return from** its audio inputs.
   - Pick a hardware output + the corresponding input (physical loopback or an
     outboard FX unit) — the ports should enable themselves (check Audio
     Settings). Play audio through the track and confirm the processed return
     comes back inline.
3. Add **External Instrument** to a (MIDI) track.
   - Pick your synth's MIDI port under **MIDI to** and the synth's audio input
     under **Return from**.
   - Play MIDI notes/clips; confirm the synth's audio returns on the same track.
   - With track MIDI input on "All Inputs", play the synth's own keyboard:
     notes must NOT double (the synth's port is dropped from the track input).
     Local Control off on the synth remains the fix for the synth triggering
     its own voice.
4. **Freeze**: put MIDI clips on the instrument track, press **Freeze** on the
   device slot. The transport plays the clip range in real time (status shows
   progress; pressing stop or Cancel aborts). Afterwards the track holds one
   audio clip of the synth's return, the insert + upstream devices are
   bypassed, and offline export now bounces correctly. **Verify alignment** of
   the frozen clip against other tracks; trim `Latency (ms)` and re-freeze if
   the hardware round-trip needs compensation. **Unfreeze** restores the
   original clips and bypass states.
5. Export with an un-frozen external insert: the export dialog flow warns and
   offers Export Anyway / Cancel.
6. Save + reload: send/return + latency persist; a frozen track stays frozen
   (unfreeze after reload must restore the stashed clips); saved ports on a
   fresh machine/device enable themselves on load.

## Known gaps / things to watch

- **Freeze capture alignment** is derived from the block edit-time at the tap
  node (auto-PDC + `manualAdjustMs` included). Needs verification with a
  physical loopback; a constant offset means trimming `manualAdjustMs`.
- The freeze pass records arrangement clips only; session-view clips on the
  track are left untouched (and un-stashed).
- Port-conflict warnings refresh when a slot rebuilds or its own selection
  changes — another slot's change doesn't live-refresh an open slot.
- Track MIDI-out suppression is the read-only mirror (not hidden).
- Inserts are not hostable inside racks (`canCreateDetached=false`).

## Key files

- `magda/daw/audio/plugins/InsertCapturePlugin.{hpp,cpp}` — hidden capture tap.
- `magda/daw/audio/insert_freeze/InsertFreezeService.{hpp,cpp}` — freeze
  orchestration; `CaptureWindowMath.hpp` — block/window mapping (unit tested).
- `magda/daw/audio/ExternalInsertDeviceEnablement.{hpp,cpp}` — port auto-enable.
- `magda/daw/audio/midi/MidiInputRouter.cpp` — sendback feedback guard.
- `magda/daw/core/ExternalInsertFreeze.hpp` — persisted freeze state
  (`DeviceInfo::externalFreeze`, serialized in `TrackSerializer.cpp`).
- `magda/daw/ui/components/chain/slot/ExternalInsertUI.{hpp,cpp}` — slot UI.
- `magda/daw/ui/windows/MainWindowExport.cpp` — unfrozen-insert export warning.
