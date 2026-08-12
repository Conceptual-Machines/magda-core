# Slice 4: time stretch and pitch (#2037)

Branch `feat/2037-clip-stretch`, off `dev/0.19.0`.

## Vendoring

- `third_party/signalsmith-stretch/` — MIT, header only: `signalsmith-stretch.h`,
  `signalsmith-linear/{fft,stft}.h`, both LICENCE files, a `SOURCES.md` naming the
  upstream tag. Copied from the fork's `3rd_party/signalsmith`, unmodified.
- `third_party/soundtouch/` — LGPL-2.1, `include/` + `source/SoundTouch/`, its COPYING,
  a `SOURCES.md`. Its own CMake target `magda_soundtouch`, unmodified sources.

**Linking**: static, both. MAGDA ships under GPL-3.0, which LGPL-2.1 permits (LGPL-2.1
s3 / GPL compatibility), so a static SoundTouch archive inside a GPL-3 binary carries no
extra obligation. Keeping it as its own unmodified target rather than folding the files
into `magda_engine` is what keeps the relink option open if MAGDA ever ships a binary
under other terms.

The fork keeps its own copies. Nothing is removed from `third_party/tracktion_engine`
here; those copies die with the fork.

`magda/engine/CMakeLists.txt` boundary check needs no new rule (it only bans
`^tracktion` in angle includes and restricts quoted ones), but the header comment grows
to name the two DSP libraries so "JUCE and the model, nothing else" stays true on paper.

## The one idea: P(t)

Everything in this slice reduces to **where in the reading the timeline instant t sits**,
and the reading is what slice 3 built: one forward file, at the device's rate.

    P(t) = anchor + (however many device samples of the reading have been consumed by t)

- **Unity**: `P(t) = anchor + (t - start) * sr`. What slice 3 does today.
- **Speed ratio**: consumed per output sample is `speedRatio`, constant, resolves in the
  snapshot: `P(t) = anchor + (t - start) * sr * speedRatio`.
- **Auto tempo**: the ratio is `projectBpm(t) / interpBpm` and moves with the tempo
  curve, so it cannot be resolved to seconds ahead of the block. But its integral is
  beats: `P(t) = anchor + (beat(t) - startBeat) * 60 / interpBpm * sr`. Exact, pure, and
  needs only the beat face of `BlockInfo`. No second tempo map on the audio thread.
- **Analog pitch**: rate multiplied by `2^(semitones/12)`, no stretcher.
- **Speed ramp fades**: `t` itself is remapped inside the fade region before P is asked,
  and the local rate is what the ramp curve's slope says.
- **Warp (#2038)**: another P(t). Nothing else in this slice moves for it.

So `clip/EventPlacement.hpp` grows one function: the reading position at an instant, in
fractional device samples, given both faces of that instant. The pool cues with it, the
voice reads with it, exactly as slice 3 does with `placementFor`.

Rounding: a block reads `round(P(t1)) - round(P(t0))` samples. Derived from the timeline
rather than accumulated, so nothing drifts and a locate needs nothing said about it.

## Who does the stretching

Above the reading chain, below the voice's fades and gain. Not in the chain: that layer
is documented as pure random access ("two reads that overlap agree to the last bit"), and
a stretcher is sequential state. `ClipVoice.hpp` already says stretch belongs above it.

**One stretcher per provisioned event, provisioned by `ClipVoicePool` and carried in
`ClipStreamTable` beside the stream.** Reasons:

- configure and allocate happen off the audio thread, which is where the pool already is;
- it is sized and moded for that exact event, so an event that asks for no stretch gets
  none and pays for nothing (the same rule as the reading chain);
- it dies with the reader table, so it never enters a plan epoch: **that is the answer to
  what a plan swap does to it, which is nothing**;
- the pool's `Reader` identity check already rebuilds an entry when what it was opened
  for changes, so a speed or mode edit gets a fresh stretcher for free.

Per block the voice asks the stream for `round(P(t1)) - round(P(t0))` samples and hands
them to the stretcher for this block's `numSamples` of output. The per-block ratio is
therefore whatever those two counts say, which is what makes a moving auto-tempo ratio
and a speed ramp cost nothing extra.

## The state question

- **A seek / a locate**: `block.continuous` is false, the stream seeks (slice 3), the
  stretcher resets and re-primes. Never rebuilt, never allocated on the audio thread.
- **A loop wrap**: nothing. Tiling is below the stream (slice 3), so the stretcher sees a
  discontinuity in the material and no position change at all.
- **A plan swap**: nothing. See above.
- **An underrun**: the stream delivered short, so the material restarts mid-phrase; the
  voice already treats that as not having sounded, and the stretcher re-primes with it.

## Latency

Compensated here, by pre-roll, and **not reported to the plan's latency pass**. A
`ClipAudio` op stays at zero latency, so stretched and unstretched voices on one track
stay sample aligned without the plan knowing anything about clips.

The pre-roll is material from *before* the start: the pool cues the stream at
`P(eventStart) - preRoll`, so the voice's first read is one contiguous read that begins
with the priming samples. Signalsmith gets `outputSeek` over them; SoundTouch is fed its
`SETTING_INITIAL_LATENCY` worth.

This differs from the fork's Signalsmith wrapper, which primes from material *at* the
start instead, so every reset there begins the clip roughly `inputLatency +
rate * outputLatency` late (about 120 ms at the default preset). That shows up in the
null-diff corpus (#2040) as a fixed offset on stretched clips, and it is the engine that
is right.

## Pitch

Semitones = `autoPitch ? transpose : pitchChange`, which is what the incumbent reads
(`getTransposeSemiTones(true)` vs `getPitchChange()`). Auto pitch's pitch-sequence offset
needs a pitch track the engine does not have yet: the snapshot compiler says so in a
diagnostic and the clip plays with `transpose` alone.

## Files

- `magda/engine/clip/ClipStretcher.{hpp,cpp}` — the interface, the Signalsmith and
  SoundTouch implementations, `preRollSamples()`, `reset()`, `prime()`, `process()`.
- `magda/engine/clip/EventPlacement.{hpp,cpp}` — `readingPositionAt`, the ramp remap.
- `magda/engine/clip/ClipVoice.cpp` — read at P, stretch or resample, ramp fades.
- `magda/engine/clip/ClipVoicePool.cpp` — provision a stretcher, cue with pre-roll.
- `magda/engine/clip/ClipStreamFeed.hpp` — the entry carries the stretcher.
- `magda/engine/io/SourceReaders.{hpp,cpp}` — expose the cubic interpolation slice 3
  already has, so the no-stretcher rate change (analog pitch) uses the same curve.
- `third_party/{signalsmith-stretch,soundtouch}/`, `third_party/CMakeLists.txt`.
- `tests/test_clip_stretch.cpp` — new, `[engine][clip][stretch]`.
- `docs/architecture/native-engine.md` — slice table, the reading chain diagram.

## Tests

Rolling, the way the clip rigs already do (cue, run the blocks in between, probe).

1. Speed ratio 2 consumes twice the material for the same output length; ratio 0.5 half.
2. Auto tempo at a constant tempo equals the equivalent constant ratio, sample for sample.
3. Auto tempo across a tempo ramp tracks the beat face: the material consumed over a
   block equals the beats elapsed, not the seconds.
4. Pitch alone leaves the length alone; speed alone leaves the pitch alone (measured by
   zero crossings on a sine).
5. Analog pitch changes both, and is off whenever auto tempo or warp forced a stretcher.
6. A locate mid-clip re-primes and the material after it is the material at the new
   position (aligned, not `preRoll` late).
7. A loop wrap crosses a tile boundary with the stretcher untouched.
8. Mode Off with a speed ratio still stretches, through `getEffectiveTimeStretchMode`.
9. Both SoundTouch modes run and produce the right output length.
10. Speed ramp fade in/out: material accelerates through the region and its pitch moves
    with it, unlike a gain fade.
11. Nothing allocates on the audio thread through any of it.
