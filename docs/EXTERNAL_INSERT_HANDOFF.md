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
  status line that carries the feedback-port warning. Selections write straight
  to the live plugin's `outputDevice`/`inputDevice`/`manualAdjustMs` +
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
- **MIDI feedback guard (#1623)**: `MidiInputRouter` drops the send target's
  own input ports from the track's "All Inputs" routing (same-hardware name
  matching, e.g. 'monologue KBD/KNOB' vs 'monologue MIDI OUT'), so the synth
  cannot loop or double its own notes through the insert send. Arm-gated: a
  record-armed track re-admits the ports so the synth's keyboard records
  (Local Control off on the synth). Explicit port selection and session-slot
  recording are never filtered. Transport stop flushes note-offs + All Notes
  Off through every insert send so hardware never hangs.
- **Feedback-port warning (#1623)**: the slot status line warns when another
  enabled insert uses the same send or return port, or the return equals the
  send.
- **Export capture pass (#1623 main item)**: offline export cannot capture
  outboard gear, so export runs it under the hood — when a routed insert
  exists, the export range first plays once in real time (progress + Cancel)
  while a hidden `InsertCapturePlugin` after each insert records its
  PDC-aligned return to a temp wav; the taps then flip to playback mode and
  substitute the recordings during the offline render (gated on
  `PluginRenderContext::isRendering`), then vanish with their files. No button,
  no project mutation. Orchestrated by `InsertRenderCaptureService` (owned by
  TracktionEngineWrapper).

Build + `magda_tests` (incl. `test_external_insert_registry.cpp`,
`test_insert_capture.cpp`) are green. **Runtime behaviour is compile/test
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
4. **Record the synth's keys**: arm the track (its ports come back into "All
   Inputs"), set Local Control OFF on the synth, record. Notes must land in
   the clip and nothing may hang after stop.
5. **Export**: with the insert routed, exporting first plays the range in
   real time (progress box, Cancel aborts the export) and the rendered file
   must contain the hardware audio. **Verify alignment** against other tracks;
   trim `Latency (ms)` and re-export if the round-trip needs compensation.
6. Save + reload: send/return + latency persist; saved ports on a fresh
   machine/device enable themselves on load.

## Known gaps / things to watch

- **Capture alignment** is derived from the block edit-time at the tap node
  (auto-PDC + `manualAdjustMs` included). Needs verification with a physical
  loopback; a constant offset means trimming `manualAdjustMs`.
- Port-conflict warnings refresh when a slot rebuilds or its own selection
  changes — another slot's change doesn't live-refresh an open slot.
- Track MIDI-out suppression is the read-only mirror (not hidden).
- Inserts are not hostable inside racks (`canCreateDetached=false`).

## Key files

- `magda/daw/audio/plugins/InsertCapturePlugin.{hpp,cpp}` — hidden tap
  (capture + render-playback modes).
- `magda/daw/audio/insert_capture/InsertRenderCaptureService.{hpp,cpp}` —
  export capture pass; `CaptureWindowMath.hpp` — block/window mapping (unit
  tested in `test_insert_capture.cpp`).
- `magda/daw/audio/ExternalInsertDeviceEnablement.{hpp,cpp}` — port auto-enable.
- `magda/daw/audio/midi/MidiInputRouter.cpp` — sendback feedback guard.
- `magda/daw/engine/TracktionEngineWrapperTransport.cpp` — note-off flush on
  stop (`sendAllNotesOffToExternalInserts`).
- `magda/daw/ui/components/chain/slot/ExternalInsertUI.{hpp,cpp}` — slot UI.
- `magda/daw/ui/windows/MainWindowExport.cpp` — capture pass wiring + progress.
