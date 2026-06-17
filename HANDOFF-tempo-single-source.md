# Tempo: single source of truth (epic)

Branch: `feat/tempo-single-source` (off `dev/0.12.0`).

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
- [ ] Phase 0  - [ ] Phase 1  - [ ] Phase 2  - [ ] Phase 3  - [ ] Phase 4  - [ ] Phase 5
