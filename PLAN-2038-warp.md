# Slice 5: warp markers, beat detection and loop info (#2038)

Branch `feat/2038-clip-warp`, off `dev/0.19.0`.

Three things that all answer the same question from different sides: where a clip's
own musical time comes from, and where it stops agreeing with the file's. Warp is
the playback half; beat detection and loop info are the two analyses the model seeds
itself from, and both are things MAGDA gets from Tracktion today.

## Part A: warp

### Nothing in the voice moves

Slice 4 turned playback into one function, `readingPositionAt`, and the voice already
asks it twice a block and reads `round(P(t1)) - round(P(t0))` samples. A warped clip is
a clip whose P bends. `ClipVoice.cpp` is untouched by this slice, and so is the
stretcher, the pool, the stream and the reading chain.

What changes is one branch in `readingPositionAt` and one new compile step.

### The map, and its inverse

The model holds `(sourceTime, warpTime)` pairs in source seconds, piecewise linear
between them, slope 1 outside the marker range (`AudioEvent::warpedSourceSeconds`).
That direction answers "where does this bit of file land musically", which is the
question the editors and the loop-region beat views ask.

Playback asks the other one. At timeline instant t the material has reached some warp
time, and what a reader needs is the source second that sits there. So the compiler
inverts it.

Warp puts the event in the beat domain: the model already says so
(`sourceInstantToTimelineBeats` scales by `interpBpm` when `autoTempo || warpEnabled`),
so warped elapsed comes off the beat face exactly as auto tempo's does, and the two
branches are one branch:

    warpedElapsed = (beat - event.span.startBeat) * 60 / interpBpm
    sourceSeconds = warp.sourceSecondsAt(warp.sourceToWarpSeconds(anchorSeconds) + warpedElapsed)
    P(t)          = sourceSeconds * deviceSampleRate

The anchor goes through the forward map because the event's start is a source position
and the elapsed is warp time: they have to be added in the same domain. This is a strict generalisation of the incumbent, whose warp path has no
anchor at all (`AudioSegmentList::create` builds segments straight from the warp
regions and never reads the clip's offset), and it degenerates to the incumbent when
the anchor is zero. A clip whose head is trimmed keeps its markers pinned to the file,
which is the only behaviour a user would call correct.

### Compiled, not carried

`AudioEventPlayback` carries `std::vector<WarpMarker>` today, raw, straight off the
model. It gets a compiled `WarpMap` instead:

- sorted and strictly increasing on both sides, so either direction is one
  `std::upper_bound` and a lerp;
- monotonic, with non-monotonic markers dropped and a diagnostic naming the clip: a
  map that goes backwards has no inverse, and the compile is where that is a fact
  about the model rather than a division by a negative span on the audio thread;
- coincident markers collapsed, so no segment has zero span and no lookup divides by
  zero;
- empty when warp is off or nothing survives, which makes the map identity and every
  caller warp-agnostic exactly as the model's is.

Not a cursor. Sequential stepping is the obvious optimisation and it is wrong here:
a locate breaks it, and a reversed read walks the map the other way. A binary search
over a per-clip vector is nine comparisons twice a block, which is not a cost worth
that class of bug.

### Reverse

The incumbent bakes warp into a rendered proxy file and can only bake one thing per
clip: `WaveAudioClip::createRenderJob` checks `getIsReversed()` first and returns the
reverse job before it ever reaches the warp one. So a reversed warped clip in
Tracktion plays reversed and unwarped, and its markers are silently lost. That is a
constraint of the proxy architecture rather than a semantic, and computing both live
has no such constraint.

So reverse and warp compose here. **The map is not mirrored at compile time.** A
reversed event walks the map backwards from the far end of what it reads and mirrors
the answer, exactly as an unwarped reversed event's anchor is mirrored. Mirroring the
map instead would need the length of the region the event reads, which is itself an
answer from the map, and the two would define each other.

Fixing that circularity fixes something older with it: `regionOf` was the event's span
at unity, so slice 4 already placed a reversed *stretched* clip's anchor with the
wrong region length. It is now the exact material the event consumes, warp and rate
included.

### Composition

- **Loop.** The one case where the reading chain cannot do its own tiling. Folding
  below the stream works because the reading advances linearly, and under warp it does
  not: a position that had already been through the map would fold in the wrong domain,
  the map would go on extending at slope 1 past the loop's end, and every pass after
  the first would play straight. So a warped loop folds in warp time, above the map,
  and `sourceReadFor` leaves the tiling below switched off. The reading saws back at
  each wrap instead of climbing, which the stream reads as a seek: one per pass, which
  is what a warped loop costs and is bounded.
- **Speed ratio.** Multiplies the warped elapsed. Warp says how the file's own time
  bends; speed says how fast the whole thing runs.
- **Speed-ramp fades.** Already applied to the beat face before the map is asked, so
  a ramped warped clip needs nothing new: `ramped()` runs first, warp second.
- **Analog pitch.** Excluded by the model already (`isAnalogPitchActive` is false when
  warp is on), so nothing to do.
- **The stretcher.** Warp is a ratio that changes at every marker, and the per-block
  ratio is whatever the two P lookups say, which is the mechanism slice 4 already
  built for auto tempo. `readingRateOf` sizes the stretcher against
  the map's *steepest* segment rather than its average, because a warped event has no
  single rate; a map that asks for more than the clamp gets a diagnostic rather than a
  scratch buffer overrun.

### Where a marker lands inside a block

The ratio is constant across a block: the voice asks for P at the two ends and hands
the difference to the stretcher. A marker crossing mid-block is therefore averaged
over that block, and the position is exact again at the next boundary because P is
derived from the timeline rather than accumulated. The incumbent has the same
property by a different route (its segments are constant-ratio and crossfaded at
0.01s). Splitting the block at marker crossings is possible and not done: it would
put a variable number of stretcher calls in a block for an error bounded by half a
block of a ratio difference, and the null-diff corpus is the place to find out
whether that is audible.

## Part B: beat detection

`autoDetectBeats` and `beatSensitivity` are on the model already
(`AudioEvent`, wired through `ClipInspectorSections`). What is behind them is
`te::WarpTimeManager::detectTransients`, via `WarpMarkerManager`.

The fork's detector is fully portable and small enough to reimplement exactly rather
than approximately. It is time domain, not spectral:

1. a first pass over the file for its peak, to normalise by;
2. two one-pole envelope followers in series (attack 1.0, release 0.002), then a
   differentiator, then a third follower;
3. threshold at `-10 - sensitivity * 30` dB, a 50 ms retrigger lockout, and each
   trigger rewound half a millisecond;
4. a trim pass, repeated up to ten times, dropping any transient within 100 ms of
   the next.

Native, in `magda/engine/analysis/TransientDetector.{hpp,cpp}`, over the engine's own
`AudioFileReader` so it tests against a computed reader and no disk. Off the audio
thread by construction: it is a whole-file analysis that returns a vector.

Caching per source and per sensitivity, as the fork does (its job dedupe key is
exactly `(file, sensitivity)`).

Not rewired into `WarpMarkerManager` in this slice. The native engine is still built
bottom-up behind its tests and nothing routes to it yet; swapping the app's detector
is part of switching the engine on, not part of implementing it.

## Part C: loop info

`seedInterpretation` / `seedInterpretationFromSource` want a source BPM and a beat
count, and today those come from `te::LoopInfo`. That is not an analysis: the fork
reads them out of `juce::AudioFormatReader::metadataValues` -- the acid chunk's
`acidBeats`, `acidTempo`, `acidNumerator`, `acidDenominator`, `acidRootNote`,
`acidOneShot`, which JUCE's `WavAudioFormat` already parses.

So this is a parse, not a port of an analysis, and it belongs beside the reader:

    magda/engine/io/SourceLoopInfo.{hpp,cpp}
    SourceLoopInfo loopInfoFrom(const juce::StringPairArray& metadata,
                                double sampleRate, std::int64_t lengthInSamples);

A free function over a metadata map rather than a method on `AudioFileReader`, for
the reason the reader interface gives for being as narrow as it is: what decoded the
file is the host's business. It also makes the whole thing testable by handing it a
map, with no fixture file and no format.

`SourceLoopInfo` carries what the model seeds from and nothing else: `bpm`,
`numBeats`, `numerator`, `denominator`, `rootNote`, `oneShot`, each optional, because
"the file says nothing" and "the file says 120" are different answers and the model's
seeding rule (never overwrite what the user set) depends on telling them apart.

## Files

- `magda/engine/clip/WarpMap.{hpp,cpp}` -- new. The compiled, inverted, monotonic map.
- `magda/engine/clip/ClipSnapshot.hpp` -- `WarpMap` replaces `std::vector<WarpMarker>`.
- `magda/engine/clip/ClipSnapshotCompiler.cpp` -- compile it, diagnose what it drops.
- `magda/engine/clip/ClipSnapshotDump.cpp` -- dump segment count and extent.
- `magda/engine/clip/EventPlacement.cpp` -- `warpedReadingSample`, the exact
  `regionSecondsOf`, warp on the beat face, warp in `readingRateOf`'s clamp.
- `magda/engine/analysis/TransientDetector.{hpp,cpp}` -- new.
- `magda/engine/io/SourceLoopInfo.{hpp,cpp}` -- new.
- `tests/test_clip_warp.cpp` -- new, `[engine][clip][warp]`.
- `tests/test_transient_detector.cpp` -- new, `[engine][analysis]`.
- `tests/test_source_loop_info.cpp` -- new, `[engine][io]`.
- `docs/architecture/native-engine.md` -- slice table, the P(t) section.

## Tests

- The map: identity when empty, exact at markers, linear between them, slope 1
  outside, forward composed with inverse is the identity to a rounding, monotonicity
  violations dropped with a diagnostic, coincident markers collapsed.
- P(t): a two-marker map plays the first half at one rate and the second at another,
  and the rates are the marker spacing ratio; the anchor shifts the map and does not
  bend it; a trimmed head keeps markers pinned.
- Composition: warp with a speed ratio, warp reversed, warp looped, warp with a speed
  ramp, each against a hand-worked position at a handful of instants.
- The detector: a synthetic click train at known positions is found at those
  positions; sensitivity monotonically changes how many survive; the 100 ms trim
  fires on a pair 50 ms apart.
- Loop info: an acid map parses; a missing key stays unset rather than defaulting; a
  beats-and-duration pair implies the bpm the model expects.
