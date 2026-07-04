# Curve Modulator: Wavetable / Recorded Morph

Status: design draft

## What already exists (do not reinvent)

- **Curve modulator** = an `ModType::LFO` modifier with a custom curve. State
  lives in `ModInfo` (`curvePoints`, `curvePreset`, `oneShot`, MSEG
  `useLoopRegion` / `loopStart` / `loopEnd`). It attaches on device, rack, or
  track ModifierLists like any modifier.
- **One frame, audio-safe** = `CurveSnapshot` (fixed 64-point array,
  `evaluate(phase)`, preset fallback, one-shot hold, MSEG loop remap).
- **Lock-free publish** = `CurveSnapshotHolder` (double-buffered, one per
  `ModId` in a `std::map<ModId, unique_ptr<CurveSnapshotHolder>>` in
  `ModifierSync`). Wired to TE's LFO `customWaveFunction` via the static
  `evaluateCallback(phase, userData)`.
- **Curve editor UI** = `LFOCurveEditor` / `ModulatorEditorPanel`.
- **Clip-based automation UI** = `AutomationClipComponent` (move / resize-left /
  resize-right, mini curve preview, double-click to detail editor), backed by
  `AutomationManager` / `AutomationInfo` / `AutomationClipId`.

A single `CurveSnapshot` is already a "frame." The new work is a layer on top,
not a new curve system.

## The new layer, in one line

Turn the single live curve into a **bank of frames** plus an **index**, and let
the index be either authored (wavetable) or recorded over time (streamlined
morph automation).

## Two flavors of the same object

### Wavetable (spatial)

A bank of `CurveSnapshot`s with an index that morphs between them.

- `CurveSnapshotHolder` holds N snapshots instead of one.
- `evaluateCallback` reads the index, selects the two adjacent frames, and lerps
  their `evaluate(phase)` outputs. (Lerp outputs, not control points: cheaper and
  already audio-safe.)
- Phase stays the intra-cycle x it already is. Index is the new axis.
- Index is drivable by anything already in the system: macro, MIDI, another LFO.

### Recording (temporal)

Capture the index over beats while tweaking the curve, replay against the
transport.

- Streamlined: you perform the morph instead of drawing an abstract index lane.
- Renders through `AutomationClipComponent` (a clip you move / resize / loop)
  but the payload logic is "index into the frame bank," not scalar points on a
  TE parameter. This is the "automation-clip UI, not automation-clip logic"
  principle.
- Beats-domain: record `(beat -> index)`, play back `playhead-beat -> index`,
  no seconds.
- Capture should inherit automation write-mode semantics (touch / latch / write).

They are the same object: recording writes an index-over-beats performance into a
bank of captured frames. The wavetable is the content; the recording is the
index driver. A single hand-authored frame with a pinned index is exactly
today's static curve modulator (zero overhead, graceful degradation).

## Two features, one engine

Wavetable and automation clip are different features with different jobs, and the
difference is structural: **fixed width vs variable size.** The purpose dictates
the data structure.

- **Automation clip = capture the performance.** A **variable-length** collection
  of frames timestamped in beats. Time-addressed, sequential, no stable slot
  identity, count set by the take (3 frames or 3000). Deliverable is the take:
  reproduce exactly what was performed, transport-locked. You reproduce it, you
  do not reuse it.
- **Wavetable = replay the waves.** A **fixed-width** array (128 = MIDI
  resolution, so a note or CC addresses a slot directly). Random-access by a
  quantized index, every slot has a stable address. The position driver can be
  continuous and interpolate between slots, so fixed width means a stable
  addressable grid, not stepped playback. Deliverable is the reusable bank.

"Replay the waves" wants stable addressable slots -> fixed. "Capture the
performance" wants to hold exactly what happened -> variable. The size model and
the intent are the same fact, which is why the two cannot be fused into one
toggled object.

### What they share (only the frame primitive)

They do NOT share a container. They share `CurveSnapshot::evaluate(phase)` (one
frame's audio eval) plus the double-buffered lock-free publish discipline of
`CurveSnapshotHolder`. Above that they diverge:

- wavetable holder: fixed `std::array<CurveSnapshot, 128>`, continuous
  position-lerp between neighbors, MIDI value -> slot addressing. Intent = reuse.
  UI = a wavetable / morph editor.
- clip holder: variable frame list, beat-cursor lookup. Intent = fidelity.
  UI = `AutomationClipComponent` (move / resize / loop).

Two containers over one frame primitive.

### Bridge: bake = resample (one-directional, optional)

Promoting a captured performance into a fixed 128-slot wavetable is a
**resample**: evaluate the captured morph at 128 evenly spaced positions (or pick
128 representatives) and drop the timing (timing was the clip's job). That is what
"distill" concretely means. A wavetable can also be authored directly with no
recording. The clip never has to become a wavetable and usually will not.

## Engine change surface (small, local)

- `CurveSnapshot`: unchanged (it is the frame).
- `CurveSnapshotHolder`: bank of snapshots + an atomic index; `evaluateCallback`
  interpolates two frames by index. Double-buffer the bank the same way a single
  snapshot is double-buffered today.
- `ModInfo`: carry the frame bank (vector of curve-point sets) and the index
  source, alongside the existing single-curve fields.
- Publish path (`ModifierSync`): copy bank into the holder, same swap discipline
  as the current `update(modInfo)`.

## Open decision (shapes the code)

Is a **frame** captured or authored?

- **Record-driven bank**: a frame is a full `CurveSnapshot` captured at moments
  in time. Arbitrary count, needs thinning (keep a frame only when the shape
  deviates from what interpolating its neighbors predicts, RDP in morph space).
- **Authored slots**: a small fixed set of morph slots designed by hand
  (wavetable-style, few frames), index sweeps between them.

Capture path and storage differ between the two. Likely both eventually, but one
comes first.

## Secondary open questions

1. Does the index expose as a modulatable input independent of phase (drive
   phase from MIDI, index from an LFO), or stay a single recorded driver?
2. How much of the `AutomationClip` container (targeting, alias/link, session
   vs arrangement placement) does the recorded morph reuse vs keep device-local?
