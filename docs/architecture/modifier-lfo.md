# Modifier LFO engine

`magda/engine/param/ModLfo.hpp` is the runtime that moves an LFO modifier. It
replaces the old model, where a modifier was a published number that never
moved between publishes.

## Settings vs. state

Two structs, kept separate on purpose:

- `LfoSettings` is what the model says the LFO is. Flat and copyable, it rides
  in the published table and is immutable for as long as that table is. The
  drawn curve rides beside it in the table's own arena rather than inside the
  struct, so a knob move doesn't allocate a vector per modifier.
- `LfoState` is where the LFO has got to. It belongs to the runtime, not the
  table, because a knob move republishes the table many times a second, and an
  LFO that restarted on every publish would never finish a cycle.

`advanceLfo` applies one block of settings to state.

## Block-rate timing

The LFO advances once per block, from the block's first sample, because that
matches Tracktion Engine: TE advances a modifier timer at the top of the block
and every plugin reading it that block sees one value. A per-sample LFO would
diverge from the TE reference implementation on every modulated parameter in
every project — a decision left for after the native-engine port.

## Depth and polarity live outside the LFO

One LFO can drive several parameters by different amounts and directions.
MAGDA keeps that per link, applied where contributions are summed
(`ParamResolve.hpp`); the LFO itself knows nothing about it. TE's own depth,
offset and bipolar modifier parameters are held at unity for the same reason.

## Bar-relative timing (`modBarPosition`, `modBarsElapsed`, `barBeatsOf`)

A synced LFO's period is measured in bars read off the tempo map, not in beats
divided by a fixed bar length, because the two diverge across a signature
change (a change always starts a new bar — see `TempoMap.hpp`). Two bars of
4/4 followed by a 6/8 bar puts a bar line at beat 8, and 8 divided by the 3
beats a 6/8 bar lasts is 8/3 — a modifier using flat division opens every 6/8
bar two-thirds of the way through its cycle and stays wrong.

A bar's length in beats is `numerator * 4 / denominator` quarter notes (a beat
is a quarter note everywhere in the engine), so a 6/8 bar is 3 beats, not 6.
The four TE modifiers were patched to use this arithmetic rather than the
other way around, because an engine being replaced isn't a reason to carry its
bug forward, and matching keeps the null-diff parity corpus meaningful
(#2128). A block that spans a tempo or signature change sums what each span is
worth in its own bar length rather than reading one bpm/signature for the
whole block (#2340).

A stopped transport has no beats to read, so a free-running modifier measures
bars there as wall-clock time at the tempo and signature the cursor sits on.

## Rate lane indexing

The Rate lane's stored value counts from the first musical division rather
than the enum's own zero, because zero is Hertz and Hertz isn't a division —
a synced modifier whose lane ran to the bottom of its range would otherwise
resolve to "not synced". `rateTypeFromLaneValue`/`laneValueFromRateType` apply
the same shift as `AutomationInfo::makeSyncDivisionInfo` on the model side.

## `LfoSettings::skipNativeResync`

A cross-track sidechain LFO is driven only by its source track: the track it
modulates must not retrigger its phase or slam its gate on every note it
happens to be playing. An earlier version of this flag was set at LFO
creation and not restated when a source was picked later, so the destination
track kept retriggering an LFO that had stopped being its own — this flag is
re-asserted from the model on every property update instead.

## `LfoState::forceZero`

A trigger arrives mid-block, after that block's parameters are already
resolved, so the earliest a device can see the retrigger is the following
block. `forceZero` latches "output one block of nothing" so that block reads
as a transition rather than a continuation; the same block resets the phase,
matching the fork's own one-block-late sequence.
