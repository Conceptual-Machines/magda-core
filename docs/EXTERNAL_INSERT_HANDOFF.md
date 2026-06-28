# External FX / External Instrument — Handoff

Branch: `feat/external-instrument-fx` (off `dev/0.13.0`). Needs testing on a
machine with a real audio interface (+ ideally a hardware FX unit and/or a MIDI
synth). Full design is in `EXTERNAL_INSERT_PLAN.md`.

## What's implemented (Phases 1–2)

- **Device kind** `InternalDeviceKind::ExternalInsert`, backed by TE's
  `te::InsertPlugin` (`xmlTypeName "insert"`). Created/persisted via the existing
  internal-plugin path; latency auto-compensated by TE's PDC.
- **Browser**: two entries over the one kind —
  - **External FX** (`isInstrument=false`): audio send + audio return.
  - **External Instrument** (`isInstrument=true`): MIDI send + audio return.
- **Slot UI** (`ExternalInsertUI`): a send picker (audio-out for FX, MIDI-out for
  instrument), an audio-return picker, and a manual-latency (ms) field. Lists come
  from `te::InsertPlugin::getPossibleDeviceNames`; selections write straight to the
  live plugin's `outputDevice`/`inputDevice`/`manualAdjustMs` + `updateDeviceTypes()`.

Build + the registry unit test (`test_external_insert_registry.cpp`) are green.
**The slot UI is compile-verified only — not yet exercised at runtime.**

## How to test

1. `make debug && make run` (or launch the built MAGDA.app).
2. Add **External FX** to an audio track from the device browser. Open its slot.
   - Confirm the **Send to** dropdown lists your interface's audio outputs and
     **Return from** lists its audio inputs.
   - Pick a hardware output + the corresponding input (a physical loopback or an
     outboard FX unit). Play audio through the track and confirm the signal goes
     out and the processed return comes back inline.
3. Add **External Instrument** to a (MIDI) track.
   - Confirm **MIDI to** lists MIDI outputs; pick your synth's MIDI port, and set
     **Return from** to the input the synth's audio is on.
   - Play MIDI notes/clips; confirm the synth's audio returns on the same track
     (no second track needed) and records/bounces correctly.
4. Save + reload the project; confirm the send/return + latency selections persist.

## Known gaps / things to watch

- **Phase 3 — hardware device enablement is NOT done.** A picked device only
  routes if it's already enabled in TE's `DeviceManager`. If a device doesn't
  appear in the pickers or audio doesn't flow, first enable the relevant
  input/output channels in **Audio Settings**, then retry. The proper fix
  (reference-counted enablement shared with track I/O, in
  `handlePlaybackContextReallocation`) is the next phase and is the main runtime
  risk — it can disable I/O other consumers need if done wrong, so it wants
  careful testing.
- **Phase 4 — track MIDI-out suppression is NOT done.** With an External
  Instrument on a track you'll currently still see the track's own MIDI-out
  selector; don't point both at the same synth (double MIDI). To be hidden when an
  instrument insert is present.
- **Feedback guard not added.** Pointing two inserts at the same hardware port, or
  a return at the same port as the send, can feed back. No UI warning yet.
- Inserts are not hostable inside racks for now (`canCreateDetached=false`).

## Commits on the branch

- plan doc → Phase 1 (model/registry + test) → Phase 2 (slot UI + browser).
