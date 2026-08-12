# Slice 7: the null-diff corpus (#2040)

Branch `feat/2040-null-diff`, off `dev/0.19.0` once #2039 lands, because the corpus
needs the MIDI slice to have a MIDI channel to compare.

Six slices were judged by tests written beside them. A test asserts what its author
believed the rule was, and where an author misread the incumbent the test agrees with
the misreading. This slice is the one that cannot: a project rendered through both
engines asserts what a listener would hear, and neither engine gets a say in what the
other produces.

That is the whole claim. Everything below is about making the claim mean something,
which mostly means making sure that a residual can only ever be a bug.

## One model, two engines

A case is a set of model values: tracks, clips, sources, a tempo map. Both legs are
handed the same values and nothing else.

- **The incumbent leg** is what the app does today: a `te::Edit`, `TrackController`
  mapping the model tracks, `ClipSynchronizer::syncClipToEngine` per clip, then
  `te::Renderer::renderToFile` over the case's range. Not a second sync written for
  the harness. The sync layer is part of what is being validated, because a clip that
  reaches TE wrong plays wrong for exactly the same user.
- **The native leg** is `compileRenderPlan` plus `compileClipSnapshot`, a
  `ClipVoicePool` over a real file-reader factory, and `renderOffline` over the same
  range in beats.

Both at the same sample rate, into buffers, compared in memory.

**Cases are built in code, not loaded from `.mgd`.** A load produces the model, and
both legs consume the model, so a file on disk adds a step neither leg is testing and
a binary to the repo. A case is thirty lines of value initialisation, reviewable as a
diff, and the material it plays is generated beside it. The migration half of #1896,
DAWproject as a semantic cross-check and the `pluginState` migrators, is not this
slice.

**Nothing is golden.** A checked-in reference render freezes the incumbent at the
moment it was recorded, which is the one thing a parity corpus must not do: the day
the fork changes, a golden says the engine broke. Both legs render on every run.

## Where it runs

`magda_juce_tests`, because the incumbent leg is a `te::Edit`, and Edits in the Catch2
target take later tests down with them. `magda_engine` is linked into that target for
the first time, under the same `MAGDA_BUILD_NATIVE_ENGINE` guard the Catch2 side uses.

The comparator is model-only and is tested on its own in `magda_tests`. It is the one
piece here that nothing else checks, and a comparator that passes everything is a
worse outcome than an engine that fails: the corpus would report parity it never
measured. So it gets fed known-bad pairs and has to report them.

## The material is chosen so that a residual can only be a bug

This is the part that decides whether the corpus is worth having, and it is not a
threshold question.

Two correct implementations of the same thing disagree wherever they interpolate.
Between the engines stand three interpolators: the rate converter below the stream is
a four-point cubic Lagrange here and JUCE's five-point in the fork, and a stretcher is
a phase vocoder whose output depends on how its input was framed. Feed those broadband
material and the residual is tens of decibels above anything a placement bug produces,
and the only way to pass is a tolerance wide enough to hide the placement bug too.

So the material is chosen per case rather than the tolerance:

- **Where the two engines must agree sample for sample**, which is placement, trims,
  fades, loop tiling, reverse and comping, the material is impulses and steps. Nothing
  interpolates, one sample of disagreement is a residual at full scale, and a fade curve
  that differs in the fourth decimal shows up.
- **Where an interpolator or a stretcher stands between them**, which is rate
  conversion, speed ratio, analog pitch, auto tempo and the stretch modes, the material
  is band limited well below Nyquist. Interpolation error falls with the fourth power
  of the ratio of frequency to sample rate, so at a few hundred hertz both curves are
  the same curve to far below the null floor, while a wrong position, a wrong ratio or
  a dropped sample is as loud as it ever was.

A broadband stretch case is worth having for one thing only, which is measuring how far
apart the two stretchers are, so there is one and it reports its number rather than
asserting a bound. What it measures is stretcher agreement, not playback, and the
report says so on its own line.

## What near null means, and what it prints

Residual is native minus incumbent, per channel. The comparator reports peak in dBFS,
RMS in dBFS, the sample index and beat position of the first sample past the floor,
and the shift it had to apply, if any.

The floor is **-120 dBFS peak** by default. Two implementations doing the same
arithmetic in a different order differ by an ulp or two, which is around -140 dBFS at
these levels, so -120 leaves room for the order of a summation without leaving room for
anything audible. A case may declare a looser floor, and a declared floor carries the
mechanism that justifies it or it does not go in.

The report is canonical text, the way `dumpPlan` and `dumpClipSnapshot` are, and it is
printed on every run rather than only on failure:

    magda-null-diff v1
    cases=21 rate=44100 blockSize=512
    [ 1] placement.grid            null          peak=-inf    rms=-inf     shift=0
    [ 2] fades.curves              null          peak=-138.2  rms=-151.7   shift=0
    [ 3] stretch.signalsmith       shift         peak=-124.9  rms=-140.1   shift=-2048
    [ 4] midi.cc                   midi          notes=64/64  cc=ok        shift=0

Numbers that move are the point. A corpus that only prints when it fails cannot show a
residual creeping from -138 to -122, which is what an engine going subtly wrong looks
like before it goes audibly wrong.

On a failure the comparator writes three WAVs to the scratch directory, the incumbent
leg, the native leg and the residual, and prints the paths. A parity failure is diagnosed
by listening and by looking, and a test that only says "expected 1e-12, got 0.3" makes
that the reader's problem.

## Three recorded divergences, each pinned rather than tolerated

The engine already differs from the fork in three ways on purpose. Every one of them is
pinned to a number the corpus asserts, so that it stays exactly as large as it was
declared to be.

**Signalsmith priming.** The fork primes with the material at the clip's start rather
than before it, so its stretched clips begin about a window late. The case measures the
shift by cross correlation and then asserts it equals the value predicted from the
stretcher's own reported input latency. Measuring alone would fit any shift, including
one that appeared because a clip moved; asserting alone would not notice the day the
fork fixes it. Both, and then a null test on the aligned material.

**Controller density.** Messages go out on every change of the quantised value here and
on a 1/16 grid there, so a curve-driven synth receives a different stream. Curve driven
audio is out of the audio assertion entirely, and the MIDI channel below carries it.

**`midiOffset` on an unlooped arranger clip.** The fork's arranger path drops it, so
every note of such a clip lands offset between the two engines. The case declares the
shift, which is the offset itself, and the MIDI channel asserts exactly that and nothing
else. A note that moves for any other reason breaks it.

## Two ways the incumbent leg lies, and what it costs if it does

Both of these produce a confident number that is about the harness rather than about the
engines, which is the worst kind of failure a parity corpus can have: it does not look
like a bug in the harness, it looks like a bug in the engine.

**The render must stay in float.** `Renderer::Parameters::bitDepth` defaults to 16.
A sixteen-bit intermediate puts quantisation noise at about -96 dBFS, which is
twenty-four decibels above the floor, so every case would report the same residual and
that residual would be the WAV format. Dithering, off by default, would add more of it and
make the number different on every run. So the leg renders at `bitDepth = 32` with
`ditheringEnabled = false`, and asserts the file it loads back is float rather than
trusting the parameter: this is the one setting whose failure mode is a corpus that looks
calibrated and measures nothing.

**Proxies have to be finished before the render starts.** Stretch, reverse and warp all
reach playback in the fork through a rendered proxy file, generated asynchronously on the
background job pool. A render kicked off before the job completes plays silence, and the
comparator would report a full scale residual for precisely the cases the corpus was
built for. Worse, it would do it intermittently.

So the leg waits, per case, rather than sleeping: pump the message thread until every wave
clip's `getPlaybackFile()` is valid and `RenderManager::isProxyBeingGenerated` is false for
it, and only then render. A timeout **fails the case with "proxy not ready"** rather than
rendering anyway. A corpus that reports a race as a parity failure is worse than one that
does not run, because someone will spend a day inside the engine looking for it.

Two details that already bit the existing sync tests and belong here. `AudioFile::isValid`
can go true before the job has released the file, so the wait is on the render manager
rather than on the file. And an outstanding job that outlives its case reads a source file
the next case has deleted, so the leg drains the pool on the way out as well as on the way
in.

## The MIDI channel

**Where both engines are tapped is the instrument's input**, because that is the only
point that means anything: what a synth receives. It is also forced by both engines.
The plan compiler emits no `ClipMidi` op for a track whose chain consumes no MIDI, and
TE's plugin callback is where `bufferForMidiMessages` exists. So every MIDI case's track
carries one capture device, an `EngineDevice` on the native side and a `te::Plugin` on
the incumbent side, each appending what arrives with its block's timeline position.

Both produce the same artifact: a list of `(sampleInTimeline, status, data1, data2)`.

**Notes are compared exactly.** Channel, pitch, velocity, and the sample of the on and
of the off. A tolerance of one sample, and only because the two engines round a beat to
a sample through different arithmetic. Note lifetime is checked on both streams
independently before they are compared, because a hung note in the incumbent is not a
reason to accept one here.

**MPE channel assignment is compared as assigned**, not canonicalised to order of first
use. The fork's round-robin with its avoid-the-last-pitch rule was ported deliberately,
so a channel that differs is a finding rather than noise, and canonicalising would be
the corpus deciding not to look.

**Controllers are compared as a curve against a sampling of it**, which is what makes
the density divergence a non-issue and still catches everything else. The engine emits on
every change of the quantised value, so its step function *is* the quantised curve; the
fork samples that curve on a 1/16-beat grid and holds. Two things are asserted, per
`(channel, controller)` and per `(channel, pitch bend)`, after collapsing the fork's
repeats:

- **Every message the fork sends lands on the engine's curve**, to one quantised unit,
  within one grid step either way.
- **The two cover the same span**, first change to last change, to within one grid step.

Both bounds are the shape of the divergence rather than numbers chosen to pass: 1/16 of a
beat is exactly how stale the fork's staircase is allowed to be, and one unit is the
resolution the value is transmitted at. A wrong value breaks it, a curve evaluated with
the wrong tension breaks it, a stream arriving a beat late breaks it, a stream that stops
halfway breaks it, and a stream with fifty times fewer messages carrying the same values
does not.

Two things are deliberately not asserted, and the reasons are the same reason.

Agreement **instant for instant** is false by construction and gets worse the faster the
curve moves: over a dive of a hundred milliseconds the fork sends four messages to the
engine's hundred, so at most instants it has simply not sent the value the curve is at.

Equal **extremes** fail for the same arithmetic. A grid sampler misses the peak of
anything moving faster than its grid, so on that dive the fork turns round at whatever
the curve happened to be at 94 ms and never sends the value at the bottom. Missing it is
the divergence rather than evidence of one. Nothing is lost by leaving it out: a stream
that stops early is caught by the span, and a value the curve never takes is caught by
the landing test, which is the only way an extreme can be wrong rather than merely
absent.

## Two maps are compared as functions, not through a render

Both are position mappings, both move every sample of everything downstream of them, and
both are exactly diffable. Comparing them through audio would take an answer that is a
number and turn it into a waveform.

**The tempo map.** `TempoMap::beatToTime` against `te::TempoSequence`, over every tempo
map the corpus uses, sampled densely, asserted to the microsecond. It takes a whole class
of failure out of the audio residual: a beat that lands at a different second moves every
clip in the project.

It also pins the one place the two are known to be able to disagree. Native subdivides a
tempo ramp because there is no closed form across a curve; TE integrates its own way. The
render cases therefore use **step** tempo changes, so that a tempo disagreement can never
be mistaken for a clip bug, and ramps are pinned here where the answer is a number rather
than a sound.

**The warp map.** `WarpMap::sourceSecondsAt` against
`WarpTimeManager::warpTimeToSourceTime`, over the corpus's marker sets, sampled densely
across and past the marker range, asserted to the microsecond. Slope 1 outside the
markers, both directions of a non-monotonic marker list, and the markers themselves.

This is where warp coverage actually lives, and the reason is slice 5's own rule: a warped
event has no single rate, so warp forces a stretcher on. A warp case rendered as audio is
therefore two different stretch pipelines with the priming shift on top, which is exactly
what the material section refuses. The map is where the warp semantics are, it is exact,
and comparing it costs nothing.

There is still one warped **audio** case, because the map being right does not prove the
voice reads through it, but it plays band limited material and carries the pinned shift
like every other stretched case.

## The corpus

Twenty-one cases, four to eight bars each, 120 bpm and 44.1 kHz unless the case is about
not being those.

| Case | Covers | Material | Verdict |
| --- | --- | --- | --- |
| `placement.grid` | four clips on beat boundaries | impulses | null |
| `placement.trims` | left and right trim, content offset | impulses | null |
| `fades.curves` | all four shapes, in and out | steps | null |
| `fades.crossfade` | the pair the model resolves for an edge | steps | null |
| `loop.tiling` | loop start off zero, non-integer loop length | impulses | null |
| `rate.48k` | a 48 kHz file on a 44.1 kHz render | band limited | null |
| `reverse.plain` | reverse alone | impulses | null |
| `speed.ratio` | a speed ratio with no stretcher | band limited | null |
| `pitch.analog` | pitch folded into the ratio | band limited | null |
| `tempo.auto` | auto tempo across a step tempo change | band limited | shift |
| `stretch.signalsmith` | `kSignalsmith` | band limited | shift |
| `stretch.soundtouch.normal` | `kSoundTouchNormal` | band limited | shift |
| `stretch.soundtouch.better` | `kSoundTouchBetter` | band limited | shift |
| `stretch.broadband` | how far apart the two stretchers are | noise | report only |
| `warp.audio` | a warped clip actually read through the map | band limited | shift |
| `takes.comp` | a comped clip and a non-default take | impulses | null |
| `midi.notes` | notes, velocities, groove | capture | midi |
| `midi.cc` | dense CC and pitch bend | capture | midi |
| `midi.mpe` | per-note pitch expression | capture | midi |
| `midi.fold` | an odd loop length under a per-beat groove | capture | midi |
| `midi.offset` | `midiOffset` on an unlooped clip | capture | midi, shift |

`tempo.auto` carries the shift for the same reason the stretch rows do, and saying so is
the point: auto tempo is a ratio that is not one, so a stretcher runs, so the fork primes
it late. Nothing about the tempo change exempts it.

It does add a question the other stretch cases cannot ask, which is why it is worth having
rather than folding into them. The priming shift is measured once, at the clip's start, and
the null test then runs over the whole clip including the tempo change. If the fork's
lateness is a fixed number of output samples, that passes. If it drifts when the ratio
moves, the aligned residual grows after the tempo change and the case fails there, with the
first divergence sample landing on it. That is a finding rather than a nuisance, and the
comparator must not be allowed to fit a second shift to make it go away: one shift per case,
measured at the start.

`midi.fold` carries a per-beat groove template on purpose. An odd loop length under a
groove is the single trickiest thing ported in slice 6, the one that decided the table is
compiled but the lookup runs per pass, and until now it is pinned only by the engine's own
tests, which is to say by my reading of `LoopedMidiEventGenerator`. Putting the two
together in one case is what checks the reading against the thing itself.

Two things are deliberately absent.

**Overlaps, and therefore holes**, per the issue and #2003. The incumbent has no correct
behaviour to diff against and giving it one is the double work that decision avoided. A
hole is not a separate exclusion: an interior silence is what occlusion leaves behind, and
occlusion is what an overlap is, so a hole case would be an overlap case wearing a
different name. They are covered by the model suites, which pin every rule, and by
reference-executor assertions: a fully covered region renders silent, a crossfade sums to
unity, a play-through overlap sums both sources.

**Reverse together with warp.** The incumbent cannot do it at all: it bakes one rendered
proxy per clip and `WaveAudioClip::createRenderJob` returns the reverse job, so the
markers are silently lost. There is nothing to diff, and the case would be asserting that
the engine reproduces a bug. It stays in the model suites.

## Calibration is part of the slice, and it is not a tolerance exercise

The first full run is a measurement. Every case reports a number, and each number is
either at the floor or has a named mechanism. A residual with no mechanism is a bug, and
it is fixed in this slice rather than written into a bound. That is the rule the MIDI
divergence already established: change the shape of the comparison, never the size of
the allowance.

### What the first runs actually said

Twelve of twenty-one hold. Five engine bugs came out of it, all fixed, and one
prediction below is falsified.

**Fixed, found by the corpus and nothing else:**

- `renderOffline` never told the voice pool where it was, so an offline bounce of
  arrangement audio provisioned no readers and rendered silence. It now services and
  fills the pool in step, and `PrefetchThread` has a manual mode so the reading is
  deterministic rather than a race with a thread.
- The launch ramp was clamped to whatever fitted in the first block, so a clip
  de-clicked over `min(256, blockSize)` samples and never continued. It is voice state
  now.
- The launch ramp also ran on a voice starting at the beginning of its own material,
  where there is nothing to be discontinuous with. A clip whose first sample is a
  transient lost it and gained 256 samples of tail. `placement.grid` and `takes.comp`
  went from a full-scale residual to a perfect null.
- A rate that varies within a block was resolved from that block's own two ends, and a
  stretcher framed whatever sizes it was handed, so both made the output a function of
  how the callback was cut up. Clips that consume their reading at a rate are now fed
  on a grid anchored to where the event begins (`ClipVoice::renderThroughCells`). The
  block-size list went from five cases to one.
- An MPE note opened with no timbre and no pressure. The specification asks for both
  and the fork sends both, and a member channel is reused round-robin, so a synth
  inherited another note's expression. Fixing it exposed a second bug: `emit` built
  every message as three bytes, which would have made channel pressure malformed.

**Falsified: the stretch cases do not null after their shift.** The two legs prime the
vocoder differently, priming sets its initial phase state, and a vocoder's phase is
memory, so the waveforms never reconverge even with identical libraries and identical
input afterwards. Magnitude is the invariant that framing leaves intact. So those cases
become three assertions rather than one, and none of them is a loosened bound: the
pinned shift, still measured by cross correlation and still asserted against the
stretcher's reported latency; envelope timing after the shift, amplitude envelopes cross
correlated and required to peak within one sample, which is what keeps a placement bug
visible; and then a magnitude-spectrogram bound with its window and hop stated. Report
only would be the corpus deciding not to look at exactly the paths where a wrong ratio
or a dropped block would hide.

**Still open:** `rate.48k`, `speed.ratio` and `fades.speedramp` sit at residuals that are
timing shaped rather than content shaped, which is prediction 2 firing exactly as
written. The shift measurement grows sub-sample precision, and the offset is then either
a misplaced reading, which is a bug, or the constant group-delay difference between a
four-point cubic and JUCE's five-point Lagrange, which is pinned against the closed form
and aligned fractionally before requiring the null. Measured, mechanized, then nulled.

### The predictions

Three, so that the run can contradict them:

- **Fades null.** `clip/FadeCurves.hpp` was ported from the incumbent shape for shape
  precisely so this case nulls. If it does not, the port is wrong, and it is the port
  that moves.
- **The resampled paths null on band limited material.** If they do not, the difference
  is not the interpolator and something about the reading position is wrong.
- **The stretch cases null after their pinned shift.** Both engines run the same vendored
  Signalsmith 1.3.2 and the same SoundTouch 2.1pre, both configured with
  `presetDefault(channels, rate, realtime)`. If a stretch case does not null after its
  shift, the wrappers are configured differently, and the corpus has found the first
  thing it was built to find.

## Cost

The corpus runs in the ordinary test job, which runs on the machine the user is sitting
at, so it has to be quick. Twenty-one cases at eight seconds of audio each, rendered
twice, is under six minutes of material; offline both legs run far faster than realtime,
and the target is the whole corpus inside thirty seconds. The `te::Edit` is created once
per case and the shared engine is reused, since that is where the fixed cost is. Proxy
generation is the one part that is not free, and it is wall-clock time the incumbent leg
spends waiting rather than work either engine does.

If it will not fit, cases get shorter rather than fewer. A case that covers a rule covers
it in four bars.

## Files

- `tests/NullDiffCase.hpp` -- what a case is: the model values, the material recipe, the
  range, the declared verdict and its mechanism. Model-only, so both legs and the
  comparator share one definition and neither can drift from it.
- `tests/NullDiffCorpus.{hpp,cpp}` -- the table above, and the model builders behind it.
- `tests/NullDiffMaterial.{hpp,cpp}` -- deterministic source files: impulse trains, steps,
  band limited tones and one seeded noise, written to the scratch directory. No binary
  assets in the repo, and the same bytes on every machine.
- `tests/NullDiffCompare.{hpp,cpp}` -- the comparator. Residual statistics, the shift
  measurement, the MIDI stream comparison and the canonical report. Model-only.
- `tests/NullDiffNativeLeg.{hpp,cpp}` -- a case rendered through the native engine. Owns
  the JUCE-backed `AudioFileReaderFactory` the engine has never needed until now, the
  runtime factory behind clip sources and the capture device, and the offline render.
- `tests/NullDiffTeLeg.{hpp,cpp}` -- a case rendered through the fork. The Edit, the track
  mapping, the sync, the proxy wait, the capture plugin and the float render.
- `tests/test_null_diff_compare.cpp` -- the comparator against known-bad pairs.
- `tests/test_null_diff_corpus_juce.cpp` -- the runner.
- `tests/CMakeLists.txt` -- the new sources, `magda_engine` linked into
  `magda_juce_tests`, and the ctest entry.
- `docs/architecture/native-engine.md` -- slice 7 in the table, the harness entry out of
  "what is not built yet" and into a section of its own, and the three divergences
  restated as pinned numbers rather than as intentions.

## Tests

The runner is one JUCE unit test that walks the corpus, plus the Catch2 file that tests
the comparator. The corpus itself is the assertion, so what needs testing beyond it is
the thing doing the asserting.

`test_null_diff_compare.cpp`:

- **A pair that differs by one sample of placement is reported**, at full scale, with the
  first divergence at the right sample. This is the failure the corpus exists to catch and
  the one a sloppy comparator misses by aligning it away.
- **The shift measurement finds a known shift and only a known shift.** Feed it a delayed
  copy and it reports the delay; feed it a pair that differs in content and it reports no
  shift rather than the best of a bad set, because a comparator that slides until
  something matches will always find something.
- **One shift per case, measured at the start.** A pair that is aligned at the start and
  drifts apart later fails, with the first divergence landing where the drift begins,
  rather than being re-aligned region by region until it passes. This is what `tempo.auto`
  leans on.
- **A residual under the floor passes and one over it fails**, checked either side of the
  boundary rather than at it.
- **MIDI notes.** A missing note, an extra note, a wrong velocity, a wrong channel and a
  note a sample late are each reported; a note within the one-sample rounding allowance
  is not.
- **MIDI controllers, which is the interesting half.** The same curve emitted as a dense
  value-change stream and as a sparse 1/16 staircase compares equal, because that is the
  recorded divergence. Then the case that decided what is not asserted: a curve fast
  enough that the fork's grid never reaches its extreme still compares equal, with the
  test itself checking that the fork's stream really does fall short, so the omission is
  pinned rather than forgotten. Then a curve that flattens early, where the fork keeps
  emitting repeats that must not make its span look longer. Then the ones that must fail:
  a stream with the right timing and a value two units out, a stream with the right values
  arriving a beat late, a controller that stops halfway through the curve, and a
  controller only one engine sends at all.
- **The report is canonical.** The same inputs print the same text, so a corpus run diffs
  against the last one.

The runner:

- **Every case in the table**, at its declared verdict.
- **The tempo maps and the warp maps**, compared as functions before any case renders. A
  mapping disagreement fails here, once, with a number, rather than twenty-one times as a
  waveform.
- **The incumbent leg tells the truth about itself.** The buffer it hands back is float,
  and every case that generates a proxy waited for one: a case whose proxy timed out is
  reported as "proxy not ready" and never as a residual. Checked by holding a proxy job
  open and asserting the leg refuses rather than renders.
- **Block size does not change the native leg.** The same case at two block sizes renders
  identically, which `RenderContext` already requires and which the corpus is now in a
  position to prove over real material rather than over a test device.
