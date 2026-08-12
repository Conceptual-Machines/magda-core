# Slice 1: whole-project cases and the tiered oracle (#2075)

Branch `feat/2075-project-cases`, off `dev/0.19.0`.

#2040 built a rig that works and covers one thing. A case is a track with clips on it, the
comparison has four verdicts, and the mixer is never touched: every case renders at unity with
one track and a default master. This slice widens the case without widening a single tolerance.

Two things come out of it. The case becomes a project, which is what every later slice needs.
And the mixer gets its first parity coverage, which is not a side effect: `resolvePlanValues`
implements fader law, pan law, mute inheritance and solo-through-destination, and nothing has
ever compared any of it against the incumbent.

## What a case is now

Today the four verdicts conflate two independent questions, and a project makes that
untenable. `Verdict::Midi` means "no audio assertion", so a project with a MIDI track *and*
audio tracks cannot say what it wants: it has to pick one. So the two questions separate.

**What is asserted about the audio** becomes a tier, which is the determinism class from the
issue rather than a per-case preference:

| Tier | What it asserts | Cases today |
| --- | --- | --- |
| `None` | nothing; the case is carried by MIDI | the four `midi.*` |
| `Exact` | residual under the floor, nothing allowed for | fourteen |
| `Aligned` | one pinned offset, measured at the start, then `Exact` | `midi.offset`, and the three still calibrating |
| `Spectral` | pinned shift, envelope timing, magnitude bound | the four stretched |
| `Invariants` | finite, equal length, discontinuity bound, tail decay | none yet |
| `Measured` | measured and printed, asserted only to be finite | `stretch.broadband` |

**Whether the MIDI streams are compared** becomes its own flag. A case can now do both, which
is what a project with an instrument track and audio tracks is.

`Invariants` has no case in the corpus and is implemented anyway, with its own comparator
tests. It is what #1893 needs, because an external plugin has no null to give; the checks
behind it are also what #2077 asserts outside a changed graph region, so it is shared rather
than speculative. `Scripted` from the issue's list is deliberately **not** a tier here: a
launcher or a monitoring case is not an offline render at all, so it needs a different runner,
and putting it in this enum would be a promise the runner does not keep.

**The environment is recorded on the case** rather than assumed by the runner: sample rate,
block size, channel count and the material seed. The report prints a case's environment
wherever it differs from the corpus default, so a case that renders at 48 kHz says so on its
own line. Plugin versions are in the issue's list and are omitted, because nothing hosts a
plugin yet; the field goes in when #1893 gives it a value.

## The mixer cases

Six, and they are the first cases in the corpus with more than one track.

| Case | Covers | Material | Tier |
| --- | --- | --- | --- |
| `mix.summing` | three tracks summed into master | impulses | exact |
| `mix.volume` | fader law: unity, attenuation, above unity, and zero | steps | exact |
| `mix.pan` | the linear pan law across its range, including the ends | steps | exact |
| `mix.mute` | a muted track contributes nothing | impulses | exact |
| `mix.solo` | a soloed track silences its siblings | impulses | exact |
| `mix.master` | master volume and pan, applied once | steps | exact |

All arithmetic, so the material is impulses and steps and the floor is the ordinary -120 dBFS.
Nothing interpolates between the two engines on these paths, and a fader law that differs in
the fourth decimal shows up.

They are worth predicting, because the fader is ported and the pan law is a decision:

- **`mix.volume` nulls.** `faderGainFromVolume` mirrors the incumbent's fader, which stores a
  slider position rather than a gain, so it tops out at +6 dB and floors at -100 dB. If it does
  not null, the port is wrong and it is the port that moves.
- **`mix.pan` nulls.** MAGDA uses the linear law and boosts the near side. If the incumbent's
  `VolumeAndPanPlugin` is on a constant-power law, this case fails at the ends and the finding
  is that a pad is 3 dB down through a rack, which is a real bug rather than a corpus problem.
- **`mix.mute` and `mix.solo` null.** The value layer resolves both through destination routing
  and nothing has ever checked that against the incumbent's own mute and solo.

## What the incumbent leg gains

The mixer path, taken through the same code the app takes. `TrackController::setTrackVolume`
and `setTrackPan` are exactly what `AudioBridge::trackPropertyChanged` calls, and mute and solo
are `te::AudioTrack::setMute` and `setSolo` there, so the leg calls the same four things in the
same order. Master volume and pan go through the edit's master plugin, which is what
`getMasterVolumePlugin` exposes for the same reason.

Not a second mixer written for the harness, for the reason the file already gives: the sync
layer is part of what is being validated.

## Sends are out of this slice, and this is why

`TrackInfo::sends` is modelled, the compiler emits `SendTap` pre and post fader, and the value
layer resolves send levels. The native leg could render a send case today.

The incumbent leg could not. Its sends live on `te::AuxSendPlugin` instances created by
`PluginManagerSync`, which wants a `PluginManager` and the device layer behind it, and an
`AuxReturnPlugin` on the destination track to match. Standing that up inside the leg is a
larger piece of work than everything else in this slice put together, and writing the aux
plugins directly in the leg would be the second sync this file refuses.

So sends stay out, named here rather than quietly skipped, and they belong with #1892 where the
rest of the routing graph is. The case builders take sends today, so the case that covers them
is data rather than more harness.

## Files

- `tests/NullDiffCompare.hpp,.cpp` -- `AudioTier` replacing `Verdict`, the invariants
  comparison, and a report that prints a case's environment where it deviates.
- `tests/NullDiffCase.hpp` -- the tier, the MIDI flag, the seed, and the environment.
- `tests/NullDiffCorpus.cpp` -- multi-track builders, the six mixer cases, and the existing
  twenty-one restated in the new vocabulary.
- `tests/NullDiffTeLeg.cpp` -- the mixer sync, through the app's own path.
- `tests/NullDiffNativeLeg.cpp` -- nothing structural; it already resolves plan values.
- `tests/test_null_diff_compare.cpp` -- the invariants tier against known-bad pairs, and the
  report's environment line.
- `tests/test_null_diff_corpus.cpp` -- the corpus shape: every case declares a tier, every
  non-`Exact` tier carries a mechanism, every MIDI case captures.
- `docs/architecture/native-engine.md` -- section 7, the tier table.

## Tests

The corpus is the assertion. What needs testing beside it is the thing doing the asserting, so
the new comparator surface gets the same treatment the rest of it had:

- **The invariants tier reports what it is for.** A pair with a NaN, a pair of different
  lengths, a pair with a step discontinuity past the bound, and a tail that never decays are
  each reported; a pair that is merely phase-different is not.
- **The tier vocabulary cannot be abused.** A case declaring `Exact` with a raised floor and no
  mechanism is refused by the corpus-shape test, which is the rule #2040 set written down as a
  test rather than as a comment.
- **The report stays canonical**, including the environment line, so a run still diffs against
  the last one.
- **The six mixer cases**, at their declared tier, in the runner.

The twenty-one existing cases keep their results exactly. A refactor that quietly relaxed the
clip corpus would be a regression wearing a green tick, so the calibration list moves across
unchanged and is asserted in both directions as before.
