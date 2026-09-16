# Native streaming seek audit (#2699)

## Scope and baseline

Analysis only; no playback changes. Recorded 2026-09-15.

- [Issue #2699](https://github.com/Conceptual-Machines/magda-core/issues/2699).
- Development baseline: `f6543da38`, including the SoundTouch/Stop/Play fix in #2697.
- Pending dependency: [PR #2698](https://github.com/Conceptual-Machines/magda-core/pull/2698), inspected at `356156ae9715e0a58cb9242f7640bad6b87531a7`. Still open at inspection. Its retained session opening and Signalsmith priming changes are not in the development baseline.
- Existing prefetch tests passed: 10 cases, 5,246 assertions. This establishes existing behaviour, not the delayed-reader acceptance criteria below. The pending PR was inspected, not built or tested in this audit.

## Findings

### 1. A discontinuity invalidates all streamed data, even a buffered destination

`PrefetchStream::read()` calls `requestSeek()` whenever the requested position differs from `nextSample_`. That releases the current chunk, drains the filled queue, changes generation, and publishes a worker request. It does not first check whether the requested range is already resident.

Thus an arbitrary locate can miss its first block even with a full buffer and a fast disk. A forward jump within the existing buffer also takes the invalidation path. Existing tests explicitly require silence for an unprepared seek.

Source: [PrefetchStream.cpp](../../magda/engine/io/PrefetchStream.cpp), `read`, `requestSeek`, `takeNextChunk`; [test_prefetch_stream.cpp](../../tests/audio/test_prefetch_stream.cpp), “A seek drops what was read for somewhere else”.

### 2. Stopped preparation does not cover playing locates or arrangement wraps

#2697 prepares the next read while stopped. `ClipAudioSource::prepareForPlay()` is not called while playing. `TransportClock` splits callbacks at arrangement loop boundaries and marks the next segment discontinuous. `ClipVoice` then resets and primes the stretcher at the destination.

The voice pool receives the current position, not a second read position reserved for the next transport wrap. Re-cueing the same stream in advance would discard material still needed before the boundary. Supporting both sides requires retained destination audio or a separate prepared stream/window.

Source: [ClipAudioSource.cpp](../../magda/engine/clip/ClipAudioSource.cpp), `renderMaterial`; [TransportClock.cpp](../../magda/engine/transport/TransportClock.cpp), `advance`; [EngineSession.cpp](../../magda/engine/exec/EngineSession.cpp), `voices_->setPosition`; [ClipVoice.cpp](../../magda/engine/clip/ClipVoice.cpp), `renderThroughCells`.

### 3. Priming availability is lost at the stretcher interface

`ClipStretcher::readPreRoll()` clears its destination, calls `stream.read()`, ignores the delivered count, and returns the entire requested buffer. `prime()` returns `void`. A partial read therefore becomes material containing zeros, with no availability result passed to the voice.

The stream does count the short read. However, the voice's `full` result checks its subsequent ordinary input reads, not the priming read. A failed prime followed by a successful ordinary read can therefore escape the voice-level starvation report. SoundTouch may subsequently have a full output FIFO containing silence from the failed prime; filled output is not proof of available source audio.

Do not fix this by unconditionally re-priming on every short read. The voice deliberately avoids that: restarting priming keeps seeking backward and can prevent recovery. A recovery design must keep timeline progress and explicitly track incomplete priming.

Source: [ClipStretcher.cpp](../../magda/engine/clip/ClipStretcher.cpp), `readPreRoll`, `SoundTouchClipStretcher::prime`; [ClipStretcher.hpp](../../magda/engine/clip/ClipStretcher.hpp), `prime`; [ClipVoice.cpp](../../magda/engine/clip/ClipVoice.cpp), `needsPrime`, `delivered < wanted`.

### 4. Current counters do not measure audible loss

`PrefetchStream::underruns()` increments once per short read, not per missing sample or device callback. One callback may make a priming read and multiple cell reads. `ClipAudioSource::starved_` also counts missing readers/voices and incomplete voice renders; it is not a sample count.

During ordinary starvation, the stream advances past missing input to stay on time. This is intentional. Tests must check recovery position as well as missing material: playing the right samples late is still a failure.

End-of-file silence and intentional padding are not disk underruns. Test expectations must use valid source ranges, not simply `requested - delivered` across EOF.

### 5. The pending session cache covers a particular region, not all seeks

At the inspected #2698 head, session readers synchronously retain their opening before registration with the worker. A seek into that region reads the retained data while the worker fills from the cache end. Arrangement readers receive no such cache.

The requested cache size is:

`preRoll + ceil(sampleRate * 0.1 * nominalRate) + maxReadingSamples(maxBlockSize)`

Actual retained length may be shorter. Priming consumes part of this region immediately; seeking near its end leaves less protection. Its useful duration also changes with playback rate. The cache size must not be treated as an unconditional time guarantee.

The new stream test covers repeated cached reads and a handoff after explicitly filling the worker. The new session test checks attacks while the worker is paused. Neither measures all samples through a delayed handoff and recovery with multiple active streams.

Pending sources: `PrefetchStream::startAt`, `requestSeek`, `read`; `ClipVoicePool::open`; tests “A retained session opening survives repeated seeks without a reader round” and “Session loop attacks survive a wrap inside the callback with the disk worker paused”. Recheck these against the merged PR.

### 6. Worker delays include other streams and obsolete reads

`PrefetchThread::fillOnce()` walks streams serially. Each `PrefetchStream::fill()` fills all available chunks before yielding. It samples its seek request once at entry, not between disk reads. A slow read on one stream delays the others; a seek arriving mid-fill can leave the worker finishing obsolete chunks before observing the new generation.

The worker polls after a 5 ms idle wait. Avoid a blanket “one or two blocks” latency assumption: at 44.1 kHz and 64 samples, 5 ms is about 3.45 callbacks, before I/O time.

These are scheduling risks established by the control flow; their audible impact has not yet been measured. Test before selecting chunk fairness or request rechecking changes.

Source: [PrefetchThread.cpp](../../magda/engine/io/PrefetchThread.cpp), `fillOnce`, `run`; [PrefetchThread.hpp](../../magda/engine/io/PrefetchThread.hpp), `kIdleMilliseconds`; `PrefetchStream::fill`.

### 7. A long stall can require more than one refill round to catch up

When a read runs short, the audio cursor advances, but the worker's fill position is not advanced to match it. Sequential reads after the gap do not request another seek. When filling resumes, it can first produce chunks whose end is already behind the audio cursor; `takeNextChunk()` discards those chunks. A pause exceeding the pool's coverage can therefore incur additional refill rounds before current audio becomes available.

This is another reason to measure recovery after releasing the delayed reader, not just the duration of the injected delay. Any catch-up change must preserve correct priming/history and retained-cache continuation positions; blindly jumping the worker to the audible cursor is not sufficient.

## Define the protection window precisely

Use resident, usable input for the requested path, not configured maximum capacity. Distinguish forward-streamed audio, retained destination audio, and output already buffered by a stretcher.

The default chunk pool holds at most `4096 * 8 = 32768` input frames. At 44.1 kHz this represents about 743 ms at rate 1, 619 ms at rate 1.2, or 74 ms at rate 10, before accounting for consumption/occupancy. These are capacity calculations, not measured dropout-free guarantees.

For the session cache, subtract consumed priming and any offset into the retained region. At constant positive rate, remaining input frames divided by `(sampleRate * rate)` gives its input coverage in playback seconds. With changing rates, count actual cell input requests instead.

An uncached arbitrary locate has zero destination coverage, even when the old forward buffer is full. Guaranteeing its first block requires preparing the target before switching, retaining a wider region, or explicitly deferring playback. This needs a product decision; do not introduce a hidden transport delay as a streaming fix.

## Delayed-reader test design

### Deterministic harness

1. Use `PrefetchThread(false)` and explicit fill scheduling for tests that pause the whole worker. Never call `fillNow()` on a background-driven worker.
2. Express pauses in output samples, not wall-clock sleeps. Advance transport and audio callbacks while withholding fills, then resume fills at a specified sample boundary.
3. For an in-flight slow read, use a dedicated worker plus a test reader with a controlled completion gate. The test releases the read deterministically and joins the worker. Do not simulate latency by returning zero: the implementation interprets that as a stalled/truncated source.
4. Give each stream distinguishable source material. Use counting samples for exact stream continuity; use constant nonzero material and isolated attacks for stretched output. Compare DSP output with an identically configured, fully supplied control render. Account for fades and algorithm-specific transient spreading; amplitude alone cannot diagnose every missing sample.
5. Preallocate audio-thread observations. Record read ranges, delivered counts and generations in the fixture, plus existing counter deltas. No callback logging or allocations. Production availability instrumentation can follow once the metric contract is agreed.

### Boundary matrix

| Case | Injection | Required observations |
| --- | --- | --- |
| First session launch, cached retrigger | Pause after provisioning, launch at callback start and inside callback | Complete opening, no stale samples; continue through cache exhaustion rather than checking only the attack |
| Session wrap | Pause before wrap; repeat over multiple cycles | First post-wrap block, cache consumption, refill generation and handoff |
| Cached-to-streamed handoff | Resume worker just before, at, and after retained data ends | Exact continuity when ready; counted missing source frames when late; recovery at correct time |
| Stopped locate then Play | Prepare destination during stopped callbacks; vary available preparation time | Preserve #2697 behaviour, including cursor inside a 128-sample cell; characterize insufficient preparation separately |
| Playing locate, uncached | Jump forward/backward without a stopped callback | Measure immediate gap and recovery; no zero-gap promise until a preparation policy exists |
| Playing locate, already buffered target | Jump within resident range | Expose avoidable invalidation separately from disk unavailability |
| Arrangement loop | Wrap inside a callback; loop start also inside a stretch cell | Preserve outgoing tail, measure incoming first block; baseline design gap until destination preparation exists |
| Sequential disk stall | Withhold fills below/at/above actual remaining coverage | No loss while resident input suffices; exact missing valid source frames after exhaustion; no lasting timeline shift |
| Partial prime | Supply only part of the priming range, then resume | Priming shortage recorded, subsequent output damage and recovery duration measured separately |
| Concurrent streams | Gate first/middle/last registered reader; seek during an in-flight fill | Per-stream loss, fairness, obsolete work and recovery; results independent of unrelated file content |

Start with plain playback, SoundTouch normal/better, and Signalsmith. Use rates 0.8/1.2 initially, then rate limits 0.1/10 and rate changes. Cover 64/128/512-sample callbacks, 44.1/48 kHz, mono/stereo, EOF, negative priming positions, and short loop regions. Expand targeted cases rather than making every dimension a full Cartesian product.

### Measurements and acceptance

- Count unavailable **valid source frames**, separating priming from ordinary reads; do not double-count channels.
- Record short-read event counts separately from missing-frame totals.
- Measure first valid output after each boundary, longest unintended silent run, output difference from the fully supplied control, and time to recover to the correct timeline position. A DSP difference is not automatically a missing sample.
- For prepared destinations and sequential playback, assert complete output while the actual coverage suffices.
- When coverage is exhausted, assert counted source loss and bounded recovery once data is available. Establish algorithm-specific recovery bounds from tests rather than assuming one callback.
- Replace the issue's “counted underrun rather than silence” wording with “any unavoidable silence is accounted for”; counting does not itself remove silence.
- Keep the existing no-allocation and callback-partition checks when implementation changes follow.

## Suggested implementation order after #2698 merges

1. Rebase the audit baseline and verify how #2698 integrates with #2697, especially priming and stopped preparation.
2. Implement the deterministic tests and sample-based availability measurements. Confirm failures against the merged baseline before fixing them.
3. Preserve usable resident data for covered seeks where ownership/generation rules permit it; prepare arrangement loop destinations without invalidating outgoing audio.
4. Give priming an explicit availability result and design recovery that does not repeatedly seek backward.
5. Validate session handoff under concurrent reader delays; change worker fairness/request polling only if measurements justify it.
6. Decide arbitrary-locate semantics separately. No arbitrary-position cache or transport wait has been selected by this analysis.

No dependency code has been cherry-picked, and no implementation is proposed as verified yet.

## Measurements (#2700)

Deterministic delayed-reader tests on the development baseline, 44.1 kHz, plain playback, both
SoundTouch modes and Signalsmith at 0.8x and 1.2x, 64/128/512-sample callbacks. Sources:
[test_prefetch_delays.cpp](../../tests/audio/test_prefetch_delays.cpp) and
[test_streaming_delays.cpp](../../tests/engine/test_streaming_delays.cpp).
`PrefetchStream::missingFrames` counts undelivered source frames per read purpose (playback or
priming), excluding frames past the end and before sample zero of a bounded reading. Stretched
cases play a tone whose level steps every 4096 frames through a sequence that never repeats, so a
render that came back late fails the envelope comparison rather than passing it a step behind.

| Case | Result |
| --- | --- |
| Stall within resident reading | No loss, output bit-identical to the fully supplied control, including at rates 0.1 and 10 and through a speed ramp |
| Stall past resident reading | Loss counted; plain playback silent for exactly the counted frames and correct from the resume callback; stretchers back within 6.1k to 9.2k output samples of input returning; priming never re-runs |
| Stream catch-up after a long stall | Rounds = ceil(behind / (coverage - block)), confirming finding 7 |
| Stopped locate then Play, one worker round | Complete, on and inside a stretch cell |
| Stopped locate with no round, playing locate, arrangement wrap | Plain loses exactly the rest of that callback. Stretchers lose 102 to 768 playback frames plus the priming window (up to 7.5k frames): up to 8.2k output samples (186 ms) of silence, although the reader answered on the next callback |
| Session launch, filled pool | Complete |
| Session launch, paused worker | Counted as above; recovers like a missed prime |
| Session wrap or re-trigger, worker keeping up | Every pass loses the reading after the wrap in that callback; expected to change with #2698 |
| Locate inside resident audio | Pool still dropped (finding 1) |
| Seek during an in-flight fill | The worker finishes the whole old pool first; the new position sounds two callbacks later |
| One held read, three streams | Streams registered after the held one starve for the whole hold; earlier ones lose nothing |
| Partial prime | Priming shortfall counted exactly and apart from playback; `ClipAudioSource::starvedVoices` stays 0 |

Cases whose intended outcome is known but not yet met are `[!shouldfail]`: locate inside resident
audio, arrangement wrap first block, session wrap and re-trigger. The #2698 retained-opening handoff
is not on this baseline and is not measured here.

Two things the harness found: Signalsmith drew bin phases from `std::random_device` below 0.5x,
so two renders of one timeline differed (now reseeded on every prime); and a DC source through
Signalsmith settles at a different level after any gap, so stretched cases use a tone.
