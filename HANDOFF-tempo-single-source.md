# Tempo: single source of truth (epic)

Branch: `feat/tempo-single-source` (off `dev/0.12.0`).

---

## ⚑ START HERE (fresh-context handoff)

**Git:** branch `feat/tempo-single-source`, REBASED onto `dev/0.12.0` @ `15cbeeda3`
(which includes #1482 master-channel-automation). All WIP is in ONE commit on top
(`c6e1607f6` "WIP: tempo single-source ..."). Tree is clean. App + Catch2 +
JUCE test builds all compile green (verified post-rebase).

**What is DONE and correct (keep it):**
- Phase 0 — `magda::TempoMap` facade (`magda/daw/core/TempoMap.hpp`) +
  `TracktionTempoMap` (engine) over `tempoSequence`. Injected into
  `TimelineController` from `MainWindow`. Tested (`test_tempo_map_juce`).
- Phase 1 core — `TimelineState` + `TrackContentPanel` + all UI clip-seconds
  call sites (`ClipComponent`, `TrackContentPanelClipDrag`, `MainView`,
  `MainWindowCommands`, the 3 editors) route beats<->seconds through the facade
  with bpm fallback. `ClipInfo::getTimeline*(const TempoMap&)` overloads added.
- Phase 4 engine sync — `TempoLaneBridge` (lane points <-> tempoSequence,
  position-aware) + `TempoLaneSync` (live, both directions, echo-guarded, owned
  by `TracktionEngineWrapper`, gated off in tests via
  `MAGDA_NO_AUTO_TEMPO_LANE_SYNC`). Tested (`test_tempo_lane_bridge_juce`,
  `test_tempo_lane_sync_juce`). THE SYNC LOGIC IS GOOD — do not rewrite it.

**Creation + display UX — FIXED (was the wrong surfacing).** Tempo/bpm is a
global/master concern; it now lives in the #1482 master automation band:
  1. REMOVED the `View > Tempo Lane` item (`ShowTempoLane` enum + View `addItem`
     + tempoLaneShown lookup + handler in MenuManager.{hpp,cpp}, the
     AutomationManager/ControlTarget includes there, and `view.tempo_lane` in
     en.json).
  2. ADDED "Tempo" to `showAutomationMenu` (`AutomationMenu.cpp`, item id 3),
     gated to `trackId == MASTER_TRACK_ID`. Selecting it calls
     `getOrCreateLane(ControlTarget::tempo(), Absolute)` + `setLaneVisible`;
     `TempoLaneSync` engages automatically.
  3. `visibleMasterAutomationLanes()` (`MasterAutomationLanes.cpp`) now PREPENDS
     `getEditScopedLanes()` before the master track lanes, so the Tempo lane
     renders in the master band with a proper header (`paintAutomationLaneHeader`
     -> `getDisplayName()` = "Tempo" + standard buttons) — header + content both
     flow from that one helper.
  4. REMOVED the old `TrackContentPanel` pinned global-lane block entirely
     (`globalLaneComponents_`, `getGlobalLanesHeight()` + its 3 offset call sites
     now 0, the rebuild + position blocks). Edit-scoped lanes no longer render
     there.

  HOW TO SEE IT: click the automation (curve) icon on the master header (or
  right-click the master header) -> "Add New Lane..." -> "Tempo". A "Tempo" lane
  appears in the master automation band at the bottom. Drag/add points = tempo
  changes; moving the transport tempo updates the lane.

  Verified: app build green; JUCE "Tempo Map/Lane Bridge/Lane Sync" + Automation
  tests pass; Catch2 1258 (1252 pass, 6 skip, 0 fail).

**Remaining follow-ups:**
  - Constrained-editor cleanup (disable bezier/tension on the tempo lane; the
    bridge only maps point tension -> TempoSetting.curve and rebuilds Linear, so
    bezier edits snap back on the next sync — janky, not broken).
  - Deferred session-sync routing (see Phase 4 notes).

Everything below is the original plan + per-phase detail. The engine/bridge/sync
and conversion routing are sound; only the creation/display surfacing is wrong.

## Goal

`te::Edit::tempoSequence` becomes the **single source of truth** for tempo. No
scalar `tempoBPM` copies, no MAGDA-side tempo model. Every beats<->seconds
conversion goes through one position-aware facade backed by `tempoSequence`.
This removes the latent constant-tempo drift and makes a real tempo-automation
lane safe (it edits `tempoSequence` directly).

## Why

Today the UI carries shadow scalars (`TimelineState.tempo.bpm`,
`TrackContentPanel.tempoBPM`, `TimeRuler.tempo`, `GridOverlay.tempoBPM`,
`TransportPanel.currentTempo`) and converts beats<->seconds with
`beats * 60 / bpm`. That formula is only correct at a constant tempo. The
engine already does it right via `tempoSequence.toTime/toBeats`; the UI just
isn't asking it. The instant tempo varies (a tempo lane), the playhead and
audio clips would be drawn where they don't play. Unacceptable to ship; fix the
conversion layer first.

## Core design

**`TempoMap` — one facade, position-aware.**
- Interface (no TE headers; UI-safe), e.g. `magda::TempoMap`:
  - `double beatToTime(double beat) const;`   // seconds, walks the curve
  - `double timeToBeat(double seconds) const;`
  - `double bpmAt(double beat) const;`
  - (pixel/zoom math stays in the UI; the facade is pure beats<->seconds + bpm.)
- Impl `TracktionTempoMap : TempoMap` in the engine layer, holds
  `te::Edit&` / `te::TempoSequence&`, delegates to `tempoSequence.toTime(BeatPosition)`
  / `toBeats(TimePosition)` / `getBpmAt(BeatPosition)`.
- Injected into `TimelineController`; every component reaches it via
  `timelineController.tempoMap()`. UI never sees TE headers.

**Ownership inversion.** Tempo flows engine -> UI, not UI -> engine.
- `tempoSequence` owns the value (and the map).
- The transport slider *writes* `tempoSequence` (the path that already exists via
  `AudioEngineListener::onTempoChanged` -> `setTempo`) and *reads back* through
  the facade.
- A "tempoSequence changed" notification drives UI relayout, replacing the
  current UI-pushes-scalar flow. This also makes the UI correct when tempo
  changes from any source (slider, automation, undo).

**Scope limiter — what does NOT change.** The arrangement is drawn in beat
space (`beat * pixelsPerBeat`) and clips are beats-native. That is
tempo-independent and stays as-is. Only two things touch the facade:
1. genuine time boundaries: playhead (time->beat), audio clips (seconds->beats),
   seconds ruler, anything handing the engine a seconds position.
2. the scalar caches we delete.

## Phases

### Phase 0 — Facade + injection (no behavior change)
- Define `TempoMap` interface + `TracktionTempoMap` impl over `tempoSequence`.
- Construct in the engine/bridge layer (where `te::Edit&` lives), inject a
  `const TempoMap*` into `TimelineController`; add `TimelineController::tempoMap()`.
- Nothing else calls it yet. Build green. Add round-trip unit tests
  (`beatToTime`/`timeToBeat` under a multi-point tempo sequence).

### Phase 1 — Route conversions through the facade
- Reimplement `TimelineState::{secondsToBeats,beatsToSeconds,pixelToTime,timeToPixel}`
  to call the facade (keep signatures so callers don't all churn at once).
- `TrackContentPanel::{pixelToTime,secondsToBeats,beatsToSeconds,timeToPixel}` ->
  delegate to the facade.
- `ClipManager` / `ClipCommands` / `TimelineUtils` `(beats, bpm)` helpers ->
  `beatToTime(beat)` / `timeToBeat(seconds)`. Some likely disappear (the engine
  already positions clips in beats).
- Audio-clip seconds->beats, playhead time->beat, seconds ruler -> facade.

### Phase 2 — Delete the scalar caches
- Remove `TrackContentPanel.tempoBPM`, `TimeRuler.tempo`, `GridOverlay.tempoBPM`
  as conversion inputs. Pass a single bpm only where a scalar is genuinely fine
  (e.g. the transport's numeric readout).
- `TimelineState.tempo.bpm`: stop using it for conversion; derive display value
  from `bpmAt(0)` or drop it.

### Phase 3 — Notification: tempoSequence -> UI
- Emit a tempo-changed signal when `tempoSequence` mutates (slider, automation,
  undo), routed engine -> `TimelineController` -> `ChangeFlags::Tempo` -> relayout.
- Retire the UI-pushes-scalar path.

### Phase 4 — Tempo automation lane as a view over `tempoSequence`
- Creation entry for the edit-scoped Tempo lane (pinned global lane block from
  #1480). The lane reads/writes `tempoSequence` `TempoSetting`s (insert/move/
  remove) — NOT a separate MAGDA curve. No baking, no second source, no drift.
- Open sub-decision (flag): `TempoSetting` is `(beat, bpm, curve-float)`; it
  cannot represent the full bezier/tension model of `AutomationCurveEditor`. So
  the tempo lane is a **constrained** editor (points + per-segment curve factor),
  not the full curve UI. Confirm that constraint is acceptable, or we keep a
  richer MAGDA curve and accept it must project down to `TempoSetting` on write.

### Phase 5 — Tests / validation
- `TempoMap` conversions under a tempo ramp (round-trip, monotonicity).
- Clip + playhead geometry under varying tempo (a beat lands at the engine's time).
- Tempo lane edit -> `tempoSequence` reflects it; reload round-trips.

## Risks / notes
- Thread safety: `tempoSequence` is read from the UI thread; TE permits this.
  Document it; no lock on the read path.
- Cost: `toTime`/`toBeats` are cheap; per-paint/per-drag calls are fine.
- This is independent of the master-automation PRs (#1480/#1482 work); do not
  bundle.

## Status
- [x] Phase 0  - [~] Phase 1 (core routing done; clip-API + editor-ruler routing deferred)
- [ ] Phase 2  - [ ] Phase 3  - [~] Phase 4 (bridge core done; live wiring + UI pending)
- [ ] Phase 5

### Phase 4 notes (bridge core done; NOT yet wired live)
The existing edit-scoped Tempo lane (`ControlTarget::Kind::Tempo`,
`AutomationManager` singleton) is a DISCONNECTED MAGDA curve today:
`ControlTargetResolver::resolve(Tempo)` returns nullptr, the playback engine
skips it, editing it changes nothing. Phase 4 connects it to `tempoSequence`.

Done -- the conversion/reconcile core, proven in isolation:
- `magda/daw/engine/TempoLaneBridge.{hpp,cpp}` (engine layer; sees te::Edit +
  core AutomationInfo):
  - `writePointsToSequence(points, edit)` -- reconcile lane points INTO
    `tempoSequence` (drop tempos >0, anchor tempo[0] at beat 0 from earliest
    point, insertTempo the rest). `remapEdit=false` (clips beats-native).
  - `readPointsFromSequence(edit)` -- rebuild points FROM every TempoSetting.
  - `bpmToNormalized`/`normalizedToBpm` via `getParameterInfoForTarget(tempo())`
    (BPM 20-300 linear -- the lane's own range, so it matches the curve editor).
  - tension <-> TempoSetting.curve is identity (both clamp to [-1,+1]).
- Test `tests/test_tempo_lane_bridge_juce.cpp` ("Tempo Lane Bridge Tests"):
  bpm round-trip, write reflects in sequence, read round-trips, reconcile
  shrinks. 19 assertions green. Constrained-editor decision confirmed (per
  epic: no second source) -- points map 1:1 to TempoSetting, no bezier.

Live wiring DONE (engine side, tested):
- `magda/daw/engine/TempoLaneSync.{hpp,cpp}` -- AutomationManagerListener +
  juce::ValueTree::Listener. Lane edit (`automationPointsChanged`) ->
  `writePointsToSequence`; tempoSequence ValueTree change (slider/undo/etc) ->
  `readPointsFromSequence` -> `AutomationManager::replaceLanePoints`. Single
  `applying_` guard breaks the echo. Attaches lazily (engages when the Tempo
  lane appears). Holds a non-const ValueTree handle copy of
  `tempoSequence.getState()` (getState() is const; ValueTree is a shared handle).
- `AutomationManager::replaceLanePoints(laneId, points)` added (bulk replace +
  fresh ids + one notify).
- Owned by `TracktionEngineWrapper` (created in createEditAndBridges, reset
  first in teardown). Guarded by `MAGDA_NO_AUTO_TEMPO_LANE_SYNC` (defined in
  BOTH test targets) so the shared test engine never attaches to the
  AutomationManager singleton and perturbs other tests.
- Tests: `tests/test_tempo_lane_sync_juce.cpp` ("Tempo Lane Sync Tests") --
  seed-on-attach, lane->sequence write, sequence->lane rebuild, no-echo-settles.
  Green. App build green; Catch2 1258 RC=0 (test_automation_singleton
  unaffected since sync is disabled there).

UI create/show action DONE -- the epic is now user-reachable:
- View menu -> "Tempo Lane" (MenuManager `ShowTempoLane`, en.json
  `view.tempo_lane`). Toggles the edit-scoped lane: create via
  `getOrCreateLane(ControlTarget::tempo(), Absolute)` (checkmark shows when it
  exists), delete via `deleteLane`. Creating it makes it appear in the pinned
  global-lane block at the top of the arrangement; TempoLaneSync engages on
  `automationLanesChanged`. App build green.
- HOW TO SEE IT: run app -> View menu -> Tempo Lane. A global lane appears
  pinned at the top of the arrangement. Drag/add points = tempo changes;
  moving the transport tempo updates the lane.

Remaining Phase 4:
1. Constrained editor: the Tempo lane still uses the full AutomationCurveEditor,
   so a user CAN draw bezier handles -- but the bridge only maps point
   tension->TempoSetting.curve and rebuilds points as Linear, so bezier edits
   silently snap back on the next sync. Disable bezier/tension UI for the Tempo
   lane (points + per-segment curve only). Not broken, but janky until done.
2. Session-sync routing (deferred from Phase 1) -- now testable against a ramp.
Note: TempoLaneSync attaches to one tempoSequence state tree at construction;
if project load swaps the Edit's tempo state via setState(), the wrapper
recreates the sync with the edit -- verify load path re-runs createEditAndBridges.

### Phase 1 notes (core done)
Done — the central conversion layer now walks the tempo curve:
- `TimelineState` holds a `const TempoMap* tempoMap` (injected by
  `TimelineController::setTempoMap`, which also sets `state.tempoMap`; `state` is
  never wholesale-reassigned so it sticks). `secondsToBeats`/`beatsToSeconds` (and
  the inline conversions in `snapTimeToGrid`) route through it, falling back to
  `beats*60/bpm` only when null. All the pixel/duration helpers delegate to those
  two, so they route automatically.
- This means the `TimelineController` boundaries that already funnel through
  `state.secondsToBeats`/`beatsToSeconds` now use the facade for free: playhead
  time->beat, edit cursor, zoom-to-fit, scroll-to-time, loop/selection/punch
  seconds entry points.
- `TrackContentPanel::{secondsToBeats,beatsToSeconds,pixelToTime,timeToPixel}`
  route through `timelineController->tempoMap()` with a `tempoBPM` fallback.
- Verified: app build clean; Catch2 1258 cases RC=0; JUCE "Tempo Map Tests",
  "Arrange Beat Native Tests", "Arrangement Transport Loop Tests", "Timeline Loop
  Activation Integration Tests" all pass by name. No behavior change at constant
  tempo (facade == formula).

Clip-API pass (foundation done; site-routing partial):
- `TempoMap.hpp` MOVED `ui/state/` -> `core/` so `ClipInfo` (core) can include
  it. Includers updated: AudioEngine.hpp, TracktionTempoMap.hpp,
  TimelineController.hpp, TimelineState.hpp (`core/TempoMap.hpp` resolves because
  `magda/daw` is an include dir).
- `ClipInfo` gained position-aware overloads `getTimelineStart/End/Length(const
  TempoMap&)`. NOTE length = `beatToTime(endBeat()) - beatToTime(startBeat)`
  (NOT a direct lengthBeats conversion) so it's correct under a varying tempo.
  The old `(double bpm)` overloads stay for callers that can't reach the facade
  (audio thread / Commands capturing bpm / tests).
- Routed (message-thread, low-churn via helper bodies + singleton
  `TimelineController::getCurrent()->tempoMap()` with bpm fallback):
  `ClipComponent.cpp` (3 helpers -> 24 call sites),
  `TrackContentPanelClipDrag.cpp` (2 helpers), `MainView.cpp` (2 helpers),
  `MainWindowCommands.cpp` (2 helpers). All UI-layer, verified green.
  Pattern: `if (auto* tc = TimelineController::getCurrent(); tc && tc->tempoMap())
  return clip.getTimelineStart(*tc->tempoMap()); return clip.getTimelineStart(bpm);`

Reality check that reshaped the pass (the agent's report was WRONG on threading):
- `ClipSynchronizer` / `SessionClipScheduler` / `SessionRecorder` are
  MESSAGE-THREAD (their headers say so), not audio-thread. So they're reachable;
  but they hold `te::Edit&` and currently do `getTimelineLength(getBpmAt(0))` --
  the constant-tempo bug. Route them via the Edit's tempoSequence directly (or a
  facade over it), not the UI singleton. Higher-risk (sync path) -> separate.
- ~76 sites are DELETE-CANDIDATES not converts: e.g. TrackContentPanel:368 group
  extent is a min/max in seconds that should be beats (`placement.startBeat` /
  `endBeat()`). Per-site judgment; this is the beats-cleanup pass, distinct from
  facade routing. Do NOT blind-convert them.
- Clip editors ROUTED (28 sites): `WaveformGridComponent.cpp` (7),
  `WaveformEditorContent.cpp` (11), `MidiEditorContent.cpp` (10), via file-local
  `facadeTimelineStart/Length/End` helpers (same facade-or-bpm pattern). Editors
  display PROJECT tempo (`timeRuler_->getTempo()`), so the project facade is the
  consistent source -- no clip-local change. Verified green.
- Remaining CONVERT sites:
  - Session sync (`ClipSynchronizer` / `SessionClipScheduler` /
    `SessionRecorder`): DEFER TO PHASE 4. Every site is
    `bpm = edit_.tempoSequence.getBpmAt(TimePosition()); clip->getTimelineLength(bpm)`
    -- which is EXACTLY correct at constant tempo, so there's no live bug, only a
    latent one under a tempo lane. Route via the Edit's `tempoSequence` directly
    (NOT the UI singleton -- audio/session layer; and a local 2-line
    `toTime(endBeat)-toTime(startBeat)` helper avoids an engine-layer
    TracktionTempoMap include). Playback-critical: do it WITH a varying-tempo
    session test in Phase 4, never blind.
  - Inspector display (`ClipInspectorState`): low-risk, low-value; redirect when
    convenient.
  - Core `ClipCommands`/`ClipManager`/`ClipOperations`: mostly DELETE-CANDIDATES
    (beats cleanup) or Commands that must resolve at execute() via
    `resolveTimelineBpm` -> would need a `resolveTimelineTempoMap` equivalent.
- Editor rulers: `TimeRuler` (used by MIDI/waveform clip editors, NOT the main
  arrangement) + `GridOverlayComponent` still convert via their own scalar
  caches. They need facade injection, which couples naturally with Phase 2's
  deletion of `TimeRuler.tempo` / `GridOverlay.tempoBPM`. TimeRuler also needs a
  decision on clip-local (auto-tempo) vs global tempo before wiring.
- `TimelineUtils::{beatsToSeconds,secondsToBeats}(bpm)` — 14 sites (ClipInspector
  display + the SetTempoEvent seconds-cache recompute in TimelineController). The
  controller ones are Phase 3 territory (tempo-change notification rework).

### Phase 0 notes (done)
- `magda::TempoMap` interface: `magda/daw/ui/state/TempoMap.hpp` (TE-free, UI-safe).
- `magda::TracktionTempoMap`: `magda/daw/engine/TracktionTempoMap.hpp` — delegates to
  `tempoSequence.toTime/toBeats/getBpmAt`; resolves the current Edit via an
  `editProvider` lambda (survives Edit recreation); falls back to constant 120 BPM
  when no Edit.
- `AudioEngine::tempoMap()` (default `nullptr`, so mocks don't break); implemented in
  `TracktionEngineWrapper` (owns the facade, built in ctor with `[this]{ return getEdit(); }`).
- `TimelineController::setTempoMap()`/`tempoMap()` + member; injected from
  `MainWindow::MainComponent::setupAudioEngineCallbacks` right after `addAudioEngineListener`.
- Nothing reads the facade yet — no behavior change. Phase 1 routes conversions through it.
- Test: `tests/test_tempo_map_juce.cpp` (JUCE target, "Tempo Map Tests"). Lives in the
  JUCE target, NOT Catch2 `magda_tests`: creating a TE Edit in the Catch2 target leaves
  leaked AsyncUpdaters (no MessageManager / no `_Exit`) that SIGSEGV a later unrelated
  test. The JUCE target has `ScopedJuceInitialiser_GUI` + `std::_Exit`, so TE Edits are
  safe there. Keep future TempoMap/Edit-creating tests in the `_juce` target.
