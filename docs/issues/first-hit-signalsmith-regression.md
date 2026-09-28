# First-hit regression after PR #2698: investigation and rollback

Date: 16 September 2026.

## Reopened after the integrated listening check

Later on 16 September, the user reported a quiet first hit / audible fade again
while testing the session-transport work for #2692/#2693. That uncommitted patch
was saved separately and removed, and the normal Debug application was rebuilt
from the merged #2709 baseline (`fa994995`). The user confirmed that this
comparison build still sounded broken. The session-transport patch is therefore
not required to reproduce this report. The earlier positive listening results
remain observations of those runs, not proof that every affected case was fixed.

A read-only inspection of the running comparison instance found one arrangement
audio clip, `TEDDY_KILLERZ_drum_loop_full_19_174`, starting at beat zero with a
length of eight beats. Transport looping was disabled at inspection time. This
is a snapshot of that instance, not an assertion about all the user's listening
cases. The remote clip summary does not expose its fade or stretch settings.

The user subsequently clarified that the current failure is in Arrangement on
the loop repeat, **not on the initial Play**. Do not collapse this distinction
into another startup-only test. The clean first pass and the failing repeat are
the relevant comparison.

### Live trace: Arrangement transport wraps lose the priming input

The `FIRST-HIT-DIAGNOSTIC-2026-09-16-A` run established that the affected repeat
was the transport wrapping from beat eight to zero, with looping enabled. The
device ran at 48 kHz with 512-frame callbacks. The clip's explicit fade-in and
fade-out were both zero, and the launch de-click correction was bypassed at the
wrap because playback began at the event's own start.

On the clean start at device callback 2410, clip 2's first sample was -0.719710,
its block RMS was 0.632344, and no priming or playback input was missing. The
same values reached the device buffer. On two following wraps:

- Callback 2667: 7,200 priming frames and 512 playback frames were missing.
- Callback 2924: 7,200 priming frames and 384 playback frames were missing.

At the second wrap the new segment began with zero output. Subsequent reads
were full, but the stretcher continued producing effectively zero output until
the block starting 7,534 samples after the restart (about 157 ms). The final
device-buffer measurements showed the same loss. A later de-click invocation
on the next block cannot explain the already missing priming input and output.

This directly identifies a different failure from the earlier fresh-start
listening comparison: Arrangement has no prepared destination for a transport
wrap. It resets the stretcher and primes it with missing input. A worker that
returns on the next callback cannot recover the attack already replaced with
zeros. The existing streaming audit and expected-failure test described this
gap, but those tests had not fixed it.

The temporary diagnostic output was saved, then removed from the source after
the user reported UI flickering. Logging may contribute to that flicker; its
cause was not separately established. The loop-input loss is directly measured
and also has an independent deterministic regression case. Do not use this
finding to retroactively explain the earlier unity-rate captures that matched.

The required fix is to prepare and retain Arrangement loop-destination audio
before the boundary, including the stretcher's priming window, without
discarding the outgoing tail or doing file I/O in the callback. Acceptance must
compare the first pass and repeated passes, including a loop start inside a
stretch cell, and must include a live listening check with diagnostics disabled.

### Arrangement destination preparation

The follow-up implementation passes the transport loop and tempo map to the
voice pool. Its non-audio worker keeps readers for the loop's opening window
and builds a separate immutable region containing priming input and a bounded
playback bridge. It reads through the same resampling, reverse and source-loop
transformations as ordinary playback. A second file reader prepares that region
without seeking the reader still serving the outgoing tail.

For stretched playback, the destination begins on the same event-relative
processing cell as `ClipVoice`, including a loop marker inside a cell. Source
positions are derived from the transport tempo map. A wrap reads retained audio
immediately and asks the ordinary disk worker for its continuation.

The retained region must only take over on a seek. Letting it intercept an
ordinary forward read without advancing the stream's existing chunk cursor can
replay stale samples at the handoff, even when missing-frame counters stay zero.
Likewise, an edit must not discard a retained region while playback is consuming
it: the worker has already been sent to its end. Publication is deferred while
that region is active, and retirement occurs off the audio thread. These are
audio-continuity requirements, not just memory-lifetime requirements.

Preparation is bounded by the reader budget and bridge size. It is not a promise
that an arbitrary new loop destination can be read instantly if a user moves a
marker at the boundary or disk preparation has not completed. It introduces no
hidden transport delay. Prepared loops must preserve the attack while their
resident window covers the reader's refill time.

Validation of this follow-up on 16 September:

- Streaming-delay suite `[2700]`: 25 cases, 6,960 assertions passed. Prepared
  incoming audio matches the supplied reference exactly, and the outgoing tail
  remains intact, for plain playback, Signalsmith and SoundTouch. Coverage also
  includes repeated wraps, nonzero loop starts inside processing cells and
  short clips retained at the destination.
- Prefetch suite: 20 cases, 8,114 assertions passed, including sequential reads,
  retained-to-stream handoff and deferred replacement of active retained audio.
- Existing `[first-hit]` suite: four cases, 1,410 assertions passed.
- The normal Debug application was built with temporary diagnostics removed.
  After being asked to run `make run-console` and listen to repeated Arrangement
  loops, the user confirmed: **“yes this works”**. This is the live acceptance
  of the loop-repeat fix, separate from the automated results above. Flickering
  was not explicitly addressed in that reply, so its status remains unconfirmed.

## Follow-up after #2710: doubled SoundTouch attack at the wrap

After #2710 merged, the user reported a different symptom: SoundTouch's first
hit sounds doubled when the Arrangement loop wraps. The retained-destination
fix had already passed the user's live listening check. Do not remove that fix
or reclassify this report as the same missing-priming failure.

Read-only inspection found the same eight-beat Arrangement clip and transport
loop at 175 BPM. The source file is 120,960 frames at 44.1 kHz; the device log
reports 48 kHz / 512 frames. An isolated impulse did not reproduce the suspected
early hit. That negative control cannot establish correct timing for complex
drum material, because SoundTouch's overlap search depends on its input.

A standalone probe using the actual drum sample and the native SoundTouch
library at unity tempo found an absolute timing error. The initial output
aligned with the source, but near the loop end SoundTouch Normal matched source
audio 398 frames ahead, and Better matched 532 frames ahead (about 9 and 12 ms
at 44.1 kHz). A transport reset that plays source sample zero again can therefore
repeat an attack already exposed in the outgoing tail. Comparing only the
incoming section with another identically primed render would miss this.

The same standalone comparison after cubic resampling to 48 kHz measured
433 frames of advance in Normal and 578 in Better (again about 9 and 12 ms).
Selecting the neutral overlap position reduced the fitted lag to zero in both
modes at both sample rates. These are source-alignment measurements, not a
claim that a listener has accepted the modified application.

`TDStretch::processSamples()` searches for a new overlap offset even at neutral
tempo. Periodic material can select a positive offset although no time stretch
is required. A probe that kept the normal overlap/FIFO path and selected zero
offset at neutral tempo removed the measured advance in both modes. This is
different from the old nominal-tempo bypass, which upstream disabled because
switching paths could click when the tempo crossed unity. Do not discard the
whole priming output: that would reintroduce the missing opening transient.

The native regression uses harmonic/transient material as well as an isolated
impulse. It compares the outgoing tail with an independent plain source/resampler
control and the incoming attack with its first passage. Removing the correction
fails all four Normal/Better and equal-rate/resampling-ratio cases, with waveform
errors of 0.63–0.72. With it, the timing cases pass. The resampling fixture uses
the same 44.1-to-48 ratio at scaled sample rates; the actual-file 48 kHz
measurement above is a separate standalone probe.

The tempo-transition check holds exact unity and unity plus arithmetic epsilon
for longer than a SoundTouch batch. Constant input verifies that entering and
leaving the corrected branch does not empty its FIFO or change the level; it
does not prove click-free transitions for every waveform.

Focused validation: `[soundtouch-timing]` passed 14 assertions in two cases,
the impulse probe passed 12 in one, `[first-hit]` passed 1,416 in five,
`[2683]` passed 302 in five, and `[2700]` passed 6,960 in 25. The full
`[engine][clip][stretch]` selection passed 688 assertions in 33 cases. The normal
Debug application was built without launching it. **The user then rejected this
build in live listening: “I still hear a kind of flam on the very first hit.”**
The neutral-tempo timing correction fixes the measured regression, but it has
not resolved the user's report and must not be described as an accepted fix.
The next diagnostic must capture the live device buffer before and after the
wrap and record the actual clip's stretch settings and rate. Do not infer those
settings solely from an earlier run or from a source-relative offline test.

The next build added only temporary device-output capture and settings logging;
it did not change playback processing. The user then reported “yeah this works”.
Keep both listening results: the earlier rejection and this successful run.
Do not attribute the improvement to a new audio fix between those builds.

The diagnostic armed before the clip was ready: its snapshot had zero tracks,
and only 128 ms of its 12-second recording contained nonzero audio. Its two
backward transport movements did not provide a valid first-pass/repeated-loop
comparison. The later read-only project inspection showed the intended 175 BPM,
eight-beat loop, but that does not retroactively identify the captured material.
The capture therefore cannot explain the successful listening result.

Temporary capture code was removed and the normal Debug application rebuilt,
preserving the neutral-tempo correction. The user then confirmed the clean build:
“this sounds correct”, adding that stretching is audible when reducing BPM but
that this is normal. **The SoundTouch follow-up is accepted in live listening
with diagnostics removed.** This confirms the final build's behavior; it does
not explain why the earlier build with the same audio correction was rejected.
Keep that uncertainty and the unusable capture in the record.

The fix preserves timing at neutral tempo. It does not remove SoundTouch's
normal time-stretch character at other ratios; do not treat the user's expected
lower-BPM stretching as a remaining flam regression.

The user also mentioned a transient UI blink, most apparent with the right
panel expanded, then explicitly deferred it. No UI changes were made for that
report; its cause and resolution remain unconfirmed.

## Outcome and scope

The listening comparison identified the Signalsmith random-generator replacement
introduced by #2706 as the change to roll back. The user confirmed that the
instrumented #2698 baseline sounded correct. Adding the local transport-loop
changes was accepted as sounding correct. The next comparison added only the
Signalsmith random-generator replacement. The user subsequently reported that
version broken. Removing just that replacement restored the correct first hit.

This is an observed add/remove listening result, not a demonstrated explanation
of the internal mechanism. The waveform captures made during the investigation
did not expose the audible difference. Do not convert that missing explanation
into a claim that the reported regression did not exist.

The integrated fix restores the upstream `SignalsmithStretch<float>` instance
and its ordinary generator behavior. It removes `InstalledRandom`, the
thread-local generator pointer, `RandomScope`, the fixed seed, and reseeding on
reset/prime. It retains the separate read-purpose accounting change in
`readPreRoll`, the newer read-ahead seeking implementation, and the user's local
transport-loop edits.

The final combination with the newer seeking implementation needs a live
listening check. Passing automated checks is not a substitute for that check.

## User-visible problem

The first hit of a sample sounded softened or faded during live playback.
Initially the report concerned BEAT mode, with Signalsmith selected, in both
Session and Arrangement. Later the user also reported the symptom outside BEAT
mode. The user had already heard PR #2698 working correctly and consistently
asked that it be used as the comparison baseline.

The important acceptance criterion was what came out during ordinary playback,
not whether an exported or offline-rendered file passed a numerical comparison.
The original investigation gave offline checks and block peak measurements too
much authority. That was a mistake: neither observation established that the
user's live playback was correct.

The workflow constraint also matters: the user runs the app with
`make run-console`. The investigator builds; the user launches and listens.
Do not launch the app for the user, require temporary executable paths, or ask
the user to keep track of commit hashes. MAGDA allows only one running instance.

## Revisions and changes compared

| Revision | Relevant change |
| --- | --- |
| `e2c7f539` | Merge of #2698; confirmed-good live playback baseline |
| `fc07da28` | #2706: missing-frame accounting and delayed-reader tests, plus the Signalsmith random-generator replacement |
| `eb8f3dfe` | #2708: use resident read-ahead audio for seeks when possible |
| `3b042b2e` | Temporary startup tracing and live output-buffer capture |
| `f14ad0c7` | Earlier comparison restoring both old Signalsmith behavior and old seeking behavior |

The original working branch was `fix/2691-transport-loop` at `eb8f3dfe`.
It also contained uncommitted transport work:

- Session continuity followed the monotonic run rather than the arrangement's
  `block.continuous` flag.
- `TransportClock::samplesUntil` rounded down, with an epsilon, instead of up.
- Associated session/transport tests and comments accompanied those changes.

Those local differences must be included in a complete comparison. Comparing
only committed history was insufficient to describe the executable under test.
They were preserved during baseline testing and restored for integration.

Between the #2698 merge and `eb8f3dfe`, committed application-source changes were
confined to `ClipStretcher.cpp` and `PrefetchStream.cpp/.hpp`. The diagnostic
commit and the local transport edits added further differences. This was a
small enough set to test incrementally; broad speculation about the output
device should not have preceded that comparison.

## Listening sequence

1. The user reported the newer working checkout broken and #2698 working.
2. Startup tracing was added. It reported successful reads and equal peaks
   before and after clip gain/fades. That did not resolve the audible report.
3. Live output-buffer capture was added and compared with the source.
4. An earlier seek-only rollback still sounded broken. This was a rollback of
   the resident-read-ahead optimization, not a complete return to #2698.
5. The checkout was returned to the complete #2698 application sources, then
   instrumented without changing its stretching/seeking behavior. The user
   explicitly said this sounded perfect.
6. The local transport-loop edits were added back to that baseline. The user's
   `OK` was explicitly treated as a positive listening result before proceeding.
7. Only the Signalsmith generator replacement was added next. An initial
   positive response was followed by an explicit correction: this version was
   broken. The correction supersedes the earlier response. At this point the
   seeking optimization had only been inspected, not applied.
8. Only the Signalsmith replacement was removed and the app rebuilt. The user
   explicitly confirmed: this works.
9. The rollback was then transferred to the newer original branch, retaining
   its seeking implementation and the user's transport work.

Do not report step 7 as a successful test because an earlier response said
otherwise. Do not attribute its regression to seeking: no seeking-code change
had been made in that comparison.

The reversal in step 8 is the strongest evidence for the chosen rollback. It is
still possible for interactions or timing effects to require further work;
the investigation did not establish a sample-level mechanism for the audible
change.

## What the logs actually established

The device configuration in the reported run was 48 kHz with 512-frame callbacks,
approximately 10.67 ms per callback. The selected output was MacBook Pro Speakers.
The affected material was `TEDDY_KILLERZ_drum_loop_full_19_174.wav` from the user's
local sample library. That commercial sample is not a repository fixture.

The observed rates were:

- `0.685714`, corresponding to the logged 120/175 tempo relationship.
- `1.000000`, matching-tempo playback.

Do not infer the actual interpreted tempo from the `174` filename alone.

At rate 1 the priming request began at source sample zero and requested 7,200
frames. At the slower rate it requested 5,842 frames. The instrumented newer
code reported no missing priming/playback frames in these opening windows.
The instrumented baseline used its existing underrun-event counter instead;
that is a count of short-read events, not a count of missing frames.

The unity-rate opening peaks included 0.982490, 0.972132, 1.007200 and 0.965950.
The values before and after clip gain/fades were equal. These facts mean that
this instrumented gain stage did not reduce those block maxima. They do not
show that every sample of the transient was intact. A later peak can survive
while the beginning of an attack is attenuated.

Duplicate trace lines in console output were not evidence of duplicate audio
processing: tracing wrote to stderr and to the installed JUCE logger.

The logged version string was generated at CMake configure time. It could be
stale after incremental source changes. Source state, build completion, and
subsequent startup time were checked together; the banner alone was not a
complete build identity.

## Live captures: useful evidence with a strict boundary

The temporary capture copied the engine's output buffer after session processing
and before copying it to the device output buffers. File writing occurred on
the timer/message side using a preallocated handoff buffer. It did not record a
microphone, speaker output, or OS loopback. Calling this a speaker recording
would be incorrect.

The baseline captures were separated into a `magda-live-first-hit-pr2698`
directory. Their first 200 ms were compared directly with the earlier unity-rate
capture, without fitting an offset or gain. Both baseline recordings matched
that earlier captured buffer exactly: maximum absolute difference zero.

The source was 44.1 kHz. The comparison reference was converted to 48 kHz using
the same cubic interpolation used by the engine, rather than a different
resampling filter. The maximum absolute source-relative difference in the first
200 ms was approximately `1.01e-6`. Earlier comparisons using a different
resampling filter measured filter differences as well as playback differences.

These results establish equality at the measured buffer boundary for those
specific unity-rate runs. They do not establish perceptual equality of the live
runs. A WAV does not preserve callback scheduling, time spent preparing a block,
device acceptance, or the timing of state changes outside the recorded buffer.
The actual cause beyond the observed code change remains unproven.

The capture lasted one second. Rapid stop/start operations could put multiple
starts or silence into a single file. Whole-file alignment or gain estimation
using a later interval was consequently unreliable in some captures. The
reported exact comparison concerns the initial 200 ms, not an assertion that
all one-second captures were uninterrupted equivalent performances.

The slower-rate capture was not established to be source-identical, nor would
sample equality against an unstretched source be a valid general requirement
for time stretching.

## Why the random-generator change existed

Signalsmith randomizes bin phase evolution when its stretch factor exceeds 2,
corresponding to playback below half speed. Its normal engine obtains its seed
from `std::random_device`. Independent renders in that range can therefore
differ even when both are functioning correctly.

The newer wrapper replaced that generator with a thread-local forwarding object,
installed a per-stretcher `std::minstd_rand` during library calls, and reseeded
it with 2700 on reset/prime. This made an offline reproducibility assertion pass,
but introduced a production behavior change for the sake of a test assumption.

A separate isolated comparison of the library implementations, using the user's
material, showed zero random draws and identical output at rates 1 and 120/175,
including startup and three resets. A below-half-speed control did draw random
numbers and produced differences. This narrows the numerical explanation; it
does not overrule the live add/remove listening result.

Do not claim that random phase values directly faded the unity-rate first hit.
The evidence does not support that mechanism. Do not retain the replacement
merely because this isolated comparison could not explain the symptom.

## Automated coverage after the rollback

The regression coverage has distinct purposes:

1. The opening-transient test covers BEAT-driven rates including 120/175 and 1,
   as well as the existing rate extremes, with repeated discontinuous starts
   and retained startup material. It checks that the first transient is present
   at the beginning rather than delayed behind a priming window.
2. A source-relative attack test compares every sample of the first 200 ms in
   both channels, with BEAT on and off, at unity rate, across three starts.
   The reference is a synthetic decaying two-frequency attack beginning at a
   nonzero amplitude. It is independent of the engine output and requires no
   copyrighted sample file. No gain normalization or alignment is fitted.
3. Missing playback and priming frames must remain zero in that test. A peak
   alone is not the acceptance metric.
4. Repeatability remains tested within Signalsmith's clean stretch range,
   using the reported 120/175 rate. The previous below-half-speed bit-equality
   requirement is removed because it contradicts the upstream behavior being
   restored.
5. At below-half-speed Signalsmith rates, and ramps that cross that range,
   covered-stall tests retain missing-frame and underrun assertions. They check
   finite output and resumed output energy rather than comparing independently
   randomized samples or short-window envelopes. Exhausted-stall tests at those
   rates retain quantitative missing-frame bounds and require resumed output.
   They do not claim exact recovery alignment against an independently randomized
   control. Recovery is observed for at least the rate-dependent flush window
   plus 8,192 output samples; the previous fixed 16,384-sample window was too
   short at 0.1x. Other rates and modes retain their existing exact/envelope comparisons.
   The first test run demonstrated why this distinction matters: stock Signalsmith
   differed even in short-window envelopes at 0.1x and sample equality also failed
   on a ramp nominally at 0.8x which passes through the randomized range.

These are tests of the engine's rendering and streaming contracts. They are not
an automated reproduction of the specific audible regression. The random-
generator replacement may also pass the new source-relative checks at unity.
That limitation is explicit, not a reason to omit useful transient coverage or
to mislabel a passing test as proof that live playback is fixed.

## Live acceptance procedure

Use the ordinary `make run-console` workflow. The investigator prepares the
build and records its source state; the user starts it and judges the sound.

- Use the same source, output device, project tempo, clip settings, and level.
- Check BEAT playback at matching tempo and at 120/175.
- Check non-BEAT playback, since it was also reported affected.
- Check Session and Arrangement, including a fresh start and repeated starts.
- Let one capture finish before rapid retriggering if waveform comparison is
  needed, and record where in the signal path that capture was taken.
- Keep the confirmed-good build/source checkpoint available until the integrated
  newer-branch build has also been heard.

A successful build and passing tests should be reported separately from the
user's listening result. If the integrated build remains broken, return to the
last confirmed-good combination and isolate the remaining difference rather
than changing several audio paths at once.

## Lessons and unresolved questions

The exact production difference should have been enumerated and isolated much
earlier. Repeated reports that #2698 worked were actionable evidence. Offline
measurements were allowed to delay the straightforward add/remove comparison.
That must not happen again for a live-audio regression.

The remaining technical question is why the generator replacement affected the
heard result while the measured opening buffers matched at unity. Possible
timing or output-boundary explanations are questions to investigate, not facts
established by this session. No device, OS, or hardware defect was demonstrated.

Preserve the known-good behavior while investigating that question. Do not
reintroduce the thread-local random-generator replacement to regain deterministic
offline output unless the real-time behavior is understood and the previously
affected listening cases are verified again.

## Validation recorded for the integrated change

The Debug app and `magda_tests` built successfully on the newer branch with the
Signalsmith rollback and the restored local transport edits.

The combined selection `[first-hit],[engine][clip][stretch],
[engine][clip][streaming],[engine][transport][clock]` completed with exit status
zero: 73 cases, 72 passing and one pre-existing expected failure, with 11,517
passing assertions and three expected-failure assertions. That expected failure
is `An arrangement loop wrap plays its first block complete`, already marked
`[!shouldfail]`; it concerns the separate arrangement-loop streaming limitation.
It was not newly suppressed or marked expected as part of this fix.

After adding the final finite-sample assertion to the source-relative test,
`[first-hit]` was rebuilt and rerun: all four cases and 1,410 assertions passed.
The stretch-rate-limit case also passed independently with 122 assertions after
the recovery observation window was corrected.

These results validate the stated engine contracts. The user's successful
listening reversal applies to the controlled comparison build. The integrated
newer-branch app still needs the final listening check through `make run-console`.
