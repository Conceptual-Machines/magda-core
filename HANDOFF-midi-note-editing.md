# HANDOFF — feat/midi-note-editing

Branch: `feat/midi-note-editing` (renamed from `feat/1705-midi-note-preview`).
PR target: `dev/0.14.0` (not opened yet).
Do NOT commit this file.

## Scope (bundled onto one branch, per bundle-PRs preference)
- **#1705** MIDI editor note preview (audition) toggle.
- **#1706** modifier + wheel MIDI note velocity editing + value readout.
- Note / clip / swatch colour consistency pass.
- Unused-icon audit + removal (17 files).
- Piano-roll playhead-click fix (IN PROGRESS — see below).

## Git state
- **origin** is at `00928dc3` (pushed once). **Unpushed** local commits:
  - `37382020` Preview a note through its own clip's track in multi-clip editing (P2 review fix)
  - `d580b3a6` Set the piano-roll playhead on click; Alt disables snap
  - `7917515d` Fix snapped playhead click: snap the transport beat, not the display beat
- **Uncommitted WIP** (playhead fix + temporary logging), in:
  - `magda/daw/ui/components/pianoroll/PianoRollGridComponent.cpp`
  - `magda/daw/ui/panels/content/PianoRollContent.cpp`
- Build is green (`make debug`). CI runs on the user's local machine — build locally, never push speculatively.

## IN PROGRESS — playhead click (finish this first)
Symptom the user reported: in the piano roll, plain click did NOT move the playhead; Alt+click did.

Root cause (proven by `[PH]` logs): the click's raw beat was correct (e.g. 3.42), but it
was being snapped with `TimelineState::snapBeatsToGrid` — the **arrangement** grid — which is
so coarse it rounded 3.42 down to **0**, so the playhead jumped to the start. Alt worked only
because it skips snapping.

Current fix (uncommitted): in `PianoRollContent`’s `gridComponent_->onPlayheadPositionBeatsChanged`
lambda, snap with `MidiEditorContent::snapBeatToGrid` (the **piano-roll** grid, `gridResolutionBeats_`,
default 0.25) instead of `state.snapBeatsToGrid`. So 3.42 -> 3.5.

Interaction model now:
- Plain click -> playhead, snapped to the piano-roll grid.
- Alt+click -> playhead, unsnapped (free). Alt = disable snap.
- The old Alt+grid-line "edit cursor" trigger was removed (edit cursor still settable from the
  ruler via `timeRuler_->onPositionClicked`). Last I-beam cursor on the empty grid also removed.

Data flow: grid `mouseDown` sets `isPendingPlayheadClick_` (allows Alt) + `playheadClickNoSnap_`;
grid `mouseUp` calls `onPlayheadPositionBeatsChanged(absolutePlayheadBeatForDisplayX(x), !noSnap)`;
`absolutePlayheadBeatForDisplayX` now returns the RAW beat (no grid snap); the content lambda
applies the piano-roll snap unless `snapToGrid` is false.

### TODO to close it out
1. User verifies plain click lands on the grid line (log should read
   `[PH] content: raw=3.41968 snapToGrid=1 -> beats=3.5`).
2. **Strip all `[PH]` DBG logging** from `PianoRollGridComponent.cpp` (mouseDown, mouseUp ENTER,
   mouseUp branch, `absolutePlayheadBeatForDisplayX`) and `PianoRollContent.cpp` (content lambda).
3. Commit (the branch squashes on merge, so d580b3a6 + 7917515d + this can stay as-is, or reword).
4. Push the unpushed commits.
5. Open the PR against `dev/0.14.0`.

## What each area does (already committed unless noted)
### #1705 note preview
- `MidiEditorContent` holds `static bool notePreviewEnabled_` + `isNotePreviewEnabled()` /
  `setNotePreviewEnabled()` and a shared `syncNotePreviewToggle(SvgButton&, bool)` helper that
  swaps the mute speaker glyphs (`master_on` / `master_off`) and recolours: dimmed `TEXT_SECONDARY`
  off, `ACCENT_BLUE` on.
- Toggle lives in each editor's top-left gutter, ABOVE the fold toggle (piano roll + drum grid).
- `NoteComponent::onNotePreview` fires note-on on mouse-down / note-off on mouse-up (select/edit
  clicks only; not erase/right-click/deselect/frozen). Draw (shift-drag) auditions while held.
  Double-click add uses a one-shot: `PianoRollContent::auditionNoteOnce(clipId, note, vel, len)`
  plays then schedules a note-off via `juce::Timer::callAfterDelay`, captured by trackId+pitch so
  it never sticks.
- Multi-clip correctness (P2 fix `37382020`): grid callbacks `onNotePreview` and `onNoteAuditionOnce`
  carry the note's own `ClipId`; the host resolves the track from it, not `editingClipId_`.
- Drum grid: same, single-clip. Pad-row play buttons still audition unconditionally.

### #1706 velocity wheel
- **Shift + wheel** over a note edits velocity; if the note is in the selection the whole selection
  scales, else just that note. Shift + wheel over the empty grid edits the selection. Consumed so
  the view never scrolls (Shift's normal horizontal-scroll is inhibited while a note is hovered or
  a selection exists). Helper `isVelocityWheelGesture(mods)` + `kVelocityWheelStep = 4` in
  `NoteComponent.hpp`.
- Undo: `SetMultipleMidiNoteVelocitiesCommand` + free `adjustMidiNoteVelocities(clip, indices, delta)`
  in `MidiNoteCommands`; consecutive same-note-set edits merge, so a wheel spin = one undo step.
  Clamp [1,127].
- `VelocityReadout.hpp` — transient "v NN" badge near the cursor during the edit, both editors.

### Colour
- Notes render `deriveTrackSwatch(clip->colour)` (piano roll `getColourForClip`, drum grid) so
  note == clip == swatch (normalized hue-only). Velocity ramps brightness.
- `ColourSwatch` shows the RAW picked colour (the picker reflects the exact choice); everything
  else renders the normalized swatch.

### Icon audit (`824737b4`)
- Removed 11 registered-but-unused + 4 dead + 2 duplicate icons (de-registered from
  `magda/daw/CMakeLists.txt` and deleted from disk).

## Deferred / open (do NOT implement without a request)
- Feature flag ("ff") to gate velocity editing — user said "eventually". Natural gate: the two
  wheel entry points (`NoteComponent::mouseWheelMove` + each grid's empty-area path).
- Velocity<->colour model rethink (the #1706 follow-up design question) — left as the brightness ramp.

## Key files
- `magda/daw/ui/components/pianoroll/NoteComponent.{hpp,cpp}`
- `magda/daw/ui/components/pianoroll/PianoRollGridComponent.{hpp,cpp}`
- `magda/daw/ui/components/pianoroll/VelocityReadout.hpp` (new)
- `magda/daw/ui/panels/content/PianoRollContent.{hpp,cpp}`
- `magda/daw/ui/panels/content/DrumGridClipContent.cpp`
- `magda/daw/ui/panels/content/MidiEditorContent.{hpp,cpp}`
- `magda/daw/core/MidiNoteCommands.{hpp,cpp}`
- `magda/daw/ui/components/common/ColourSwatch.hpp`
- `magda/daw/CMakeLists.txt`
- `BottomPanel.{cpp,hpp}` — the header-button approach was tried then fully reverted; no net change.
