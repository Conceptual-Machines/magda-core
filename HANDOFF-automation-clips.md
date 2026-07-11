# HANDOFF: Automation clips (#1087) + bake modulation (#162)

Branch: `feat/clip-automation-ui` (off `dev/0.15.0`). NOT pushed. All work
builds with `make debug`; full Catch2 (`cmake-build-debug/tests/magda_tests`)
and JUCE (`.../magda_juce_tests_artefacts/Debug/magda_juce_tests`) suites green.

## UNCOMMITTED (working tree, tested, ready to commit)

Fixes for "curve editor shows nothing / clip loses focus when touched":
- `AutomationCurveEditor.hpp/.cpp`: `setClipId` / `setClipOffset` now
  invalidate the points cache (ctor built it before the clip id was set →
  editor rendered the empty lane forever); new `automationClipsChanged`
  override — clip point edits notify clips-changed, not points-changed, so
  the editor never refreshed after an edit.
- `BottomPanel.cpp` + `AutomationClipEditorContent.*`: selecting a point
  INSIDE an automation clip (SelectionType::AutomationPoint with a valid
  clipId) keeps the clip editor open — same rule as note selection keeping
  the piano roll. Editor derives its clip from either selection type and
  only rebuilds when the clip actually changes.

Backspace point-delete no longer closes the clip editor:
- `MainWindowCommands.cpp` deleteCmd + `AutomationCurveEditor::
  onDeleteSelectedPoints`: after deleting points that live in a clip, the
  selection falls back to the clip (selectAutomationClip) instead of clearing
  to nothing; lane points still clear. Dead `deleteSelectedPoints()` removed.

First point no longer cut in half at the clip edge:
- `AutomationCurveEditor::setEdgeInsetPx` (new): horizontal pixel inset in
  the coordinate mapping; the clip editor spans the full canvas (pads
  included) with a kEdgePad inset. Lanes keep inset 0 (must stay grid-aligned
  with the arrangement).

Header grid controls (AUTO/SNAP + num/den), wired per-clip like the piano
roll:
- `AutomationClipInfo` carries gridAutoGrid/gridNumerator/gridDenominator/
  gridSnapEnabled (defaults auto, 1/4, snap on); serialized in
  `AutomationModSerializer`. `AutomationManager::setClipGridSettings` /
  `setClipSnapEnabled` notify clips-changed.
- `AutomationClipEditorContent`: `wantsHeader()` true; snap/grid lambdas +
  ruler resolution read the clip's grid (auto = zoom-derived via
  `GridConstants::findBeatSubdivision`, manual = 4*num/den);
  `onAutoGridDisplayChanged` mirrors the MIDI convention (1/den while auto).
- `BottomPanel`: `addGridControlsToHeader()` (grid subset only — no loop
  toggle/tabs/fullscreen; clip loop stays in the inspector), automation
  branches in the four control handlers + `syncGridControlsFromContent`,
  layout gate includes AutomationClipEditor.

Committed since (2026-07-09, all built + suites green): clip border markers
in the editor (1px, ruler-aligned); loop-cycle unroll in the timeline clip
preview (pixel-per-beat mapping, stable during resize drags, markers hidden
under 32px/cycle); header loop button (automation branch) + inspector
arrangement/automation icons; rename (RenameAutomationClipCommand, editable
inspector name); backspace deletes selected clip; colour swatch + clips
default to track colour (AutomationManager::getLaneTrackColour); loop glyph
on clips; realtime mini-curve drag preview (drag broadcasts clip-local);
track content ghost in the editor (ghost piano roll for MIDI, shared
paintClipWaveform for audio - extracted from ClipComponent, warp/loop
identical to arrangement).

## MODEL / DESIGN DECISIONS (user's spec — do not re-litigate)

- A lane is EITHER clip-based OR free-drawn (exclusive). Mode = button on the
  lane header strip (wave glyph = curve, blocks = clips), converts data both
  ways, undoable, one-bar minimum clip when wrapping a near-empty curve.
- MIDI-clip loop model: arrangement clip has length >= loop cycle and unrolls
  visually (repeat ticks) on the TIMELINE only. The EDITOR shows the clip's
  own timeline: looped -> one loop cycle from bar 1 (rel mode, like MIDI
  loop/rel); non-looped -> real arrangement position (abs mode; the editor's
  `clipOffset` puts coordinates in timeline beats; ruler abs).
- Automation clips render translucent (0.35 fill) vs MIDI/audio.
- Clip fundamentals (Start/End/Length bars.beats, Loop, Loop Len) live in the
  Inspector (`AutomationClipInspector`).
- Gap semantics: between clips the lane holds the nearest clip edge value;
  lane with no clips = 0.5.
- Process: the user designs UX. Never pick affordances/placement — ask.

## MAP OF NEW/TOUCHED PIECES

Core:
- `core/ClipLaneFlattener.{hpp,cpp}` — pure clip-lane -> breakpoint list
  (loop unroll, gap holds, wrap jumps). Used by playback bake AND
  `convertLaneToAbsolute`.
- `core/AutomationManager` — gap-hold `getValueAtBeat`, `convertLaneToClipBased
  (minLengthBeats)`, `convertLaneToAbsolute`, `restoreLaneState`,
  `replacePointsInRange` (id-preserving), `restoreClip` re-links lane.clipIds.
- `core/AutomationCommands` — ConvertAutomationLaneType, Create/Delete/Move/
  Resize/Duplicate AutomationClip commands, BakeModulationCommand.
- `core/AutomationInfo.hpp` — `AutomationClipInfo::getEndLocalBeat()`.
- `audio/automation/AutomationPlaybackEngine` — clip lanes bake to TE
  (flattener feeds the existing emission loop); `automationClipsChanged`
  triggers rebakes. Old TODO closed.
- `audio/automation/ModulationBaker` (#162) — offline LFO-link sampler; menu
  entry "Bake Modulation to Automation" in ParamLinkMenu (via
  `params/ModulationBakeAction`). Disables baked links, undoable.
- `audio/automation/ControlTargetResolver` — shared `laneNormalizedFromTEValue`;
  `getCurrentTargetValueImpl` PluginParam reads live TE `getCurrentBaseValue()`
  (not the modulated value, not the DeviceInfo cache).
- `audio/processors/CompiledFaustProcessor` + `ParameterInfoBuilder` — mirror
  `getCurrentBaseValue()` into DeviceInfo, never the modulated value.

UI:
- `components/automation/AutomationLaneHeader` — 5th header button = lane mode
  toggle (passes bar length from time signature).
- `components/automation/AutomationLaneComponent` — dbl-click empty clip-lane
  area creates a one-bar clip; clip callbacks wired (snap, select, open
  editor); ppb propagates to clip components on zoom.
- `components/automation/AutomationClipComponent` — translucent paint, undoable
  drag move/resize, right-click menu (Edit Curve/Duplicate/Loop/Delete), edge
  resize cursors, float-accurate loop ticks.
- `panels/content/AutomationClipEditorContent` — bottom-panel editor:
  TimeRuler (linked viewport, anchor drag-zoom 2-500ppb, drag scroll,
  kEdgePad=10 aligned with curve), fit-to-clip initial zoom, selection-driven
  (registered in `PanelState.hpp` bottom tabs — REQUIRED or setActiveTabByType
  silently no-ops; that was a real bug).
- `panels/content/inspector/AutomationClipInspector` — Start/End/Length +
  Loop/LoopLen, undoable via clip commands, live two-way sync.

Tests: `tests/test_automation_clip_lanes.cpp` (flattener, gap-hold, loop wrap,
conversion round-trips, clip command undo), `tests/test_modulation_baker.cpp`.

## KNOWN GAPS / LIKELY NEXT AFTER GRID CONTROLS

- Ghost overlay sync (user-reported): fixed by mirroring
  ClipComponent::paintMidiNotes (loop unroll + offsets) in paintTrackGhost -
  verify visually against a looped MIDI clip; if still off, compare the
  audio path / getTimelineLength next.

- VERIFY SERIALIZATION: `AutomationModSerializer` round-trips lane `clipIds`,
  but confirm the clips themselves (`AutomationManager::clips_`) are saved and
  restored (`restoreClip` / project load path). Not yet checked end-to-end.
- Modulation preview overlay on lanes (deferred #162 item).
- Copy/paste automation clips; drag clip between lanes; overlapping-clip
  policy (first-in-clipIds wins at playback — no UI rule yet).
- Time-edit commands (DuplicateAutomationTimeSelection / InsertTimeAutomation)
  still skip clip-based lanes.
- Editor niceties: value scale labels on the left (lane has them, editor
  doesn't), wheel zoom, vertical drag on ruler already zooms.
- Pre-existing bug (probe-confirmed, worked around): compiled-Faust/internal
  processors never write live param changes back to DeviceInfo::currentValue
  (only ExternalPluginProcessor has the listener). Automation is now immune
  (live reads), but other cache consumers aren't.

## HOW TO SMOKE TEST

Track with any device → track header Automation button → add a lane → lane
header mode button (4th) → clip lane. Double-click empty lane = one-bar clip.
Select it: inspector shows props, bottom panel shows the big curve editor
(pencil draws, right-click point = value entry, ruler drag = zoom). Loop via
inspector; timeline clip shows repeat ticks, editor shows one cycle. Playback
follows edits immediately (bake on clips-changed). Undo works across all of
it.
