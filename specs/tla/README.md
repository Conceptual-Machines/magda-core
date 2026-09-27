# Thread handoff models

Run `make tla` to check all models locally with the pinned TLC version. Narrow a
run with `make tla ARGS="hand_back_notices"`, or add a config name such as
`make tla ARGS="hand_back_standby Safety"`. Java is required. These checks are
not part of CI. See issue #2863; `plan_swap` is the first model, added in #2866.

The [device restart and native-host teardown models](LIFECYCLE.md) cover callback
removal, generation gating, and pending-update cancellation. That document also
records why the JUCE test harness's timed drain is not a general shutdown proof.

## Destructive level taps

`level_tap` models one channel of `LevelTap`, where the audio thread folds a
block peak into an atomic slot with compare-and-swap and the message thread
destructively exchanges that slot to zero. Ghost state records which writes a
larger peak represents. The safety configuration checks that every completed
write is still represented or has been reported exactly once, and that a
reported value is at least as loud as every write it represents. The liveness
configuration checks that fair polling eventually reports every write after
the writer stops.

The model assumes the class's documented single-writer/single-reader contract.
`clear()` is outside it because the real protocol calls it only after a plan
swap removed the tap's writer. Peak values are a small ordered set and atomics
are linearizable; this is not a C++ weak-memory model. The existing
`[engine][tap]` stress test is the real-code reproduction seam.

| Configuration | Bounds | Distinct states checked |
| --- | --- | ---: |
| Level tap Safety | 4 writes, 3 nonzero peak magnitudes | 13,548 |
| Level tap Liveness | 3 writes, 2 nonzero peak magnitudes | 764 |

Both configurations passed with `tla2tools` v1.7.4. A deliberately broken
copy that split the reader's atomic exchange into separate read and reset steps
violated `NoLostPeak`; the counterexample leaves a reported write in the slot
between those two steps. This is a validation mutation, not a C++ defect.

## Session-held track hand-back

Three small models cover the separate handoffs. They are **not a composed proof
of the whole playback path**. A late notice or late prime is allowed: a voice
must reject an unsuitable standby and use its normal rendering path. Nothing
here promises that provisioning finishes before a musical deadline.

| Model | Code represented | Checks |
| --- | --- | --- |
| `hand_back_notices` | `ClipVoicePool::announceHandBacks`, `drainHandBacks` | Ring capacity; no read of an unpublished or overwritten slot; eventual delivery of the final per-track schedule, including cancellation, after changes stop |
| `hand_back_standby` | `ClipVoicePool::settleStandby`, `prepareStandby`, `service`; `StandbyStretcher::claim`; `ClipVoice::adoptStandby`, `render`; `ClipStreamFeed::publish`, `BlockScope` | At most one successful adoption or withdrawal per standby; matching prime key on adoption; live objects at use; owners keep objects alive; eventual reclamation of unowned objects and completion |
| `hand_back_cue` | `PrefetchStream::seek`, `applyPendingCue`, `read`; `ClipStreamFeed::BlockScope` | Cue generations do not go backwards; pending cues do not interrupt a stream that sounded in the previous block; the final cue is eventually applied after the stream stays silent |

The pool and voice files are in `magda/engine/clip/`; `PrefetchStream.cpp` is in
`magda/engine/io/`. Modelled code includes comments pointing back to its spec.

### Bounds and assumptions

| Configuration | Bounds | Distinct states checked |
| --- | --- | ---: |
| Notices Safety | 2 tracks, 2 ring slots, 3 arbitrary schedule changes | 23,239 |
| Notices Liveness | 2 tracks, 1 ring slot, 3 arbitrary schedule changes | 6,951 |
| Standby Safety | 3 provisioning rounds, 2 callbacks, 2 starts per callback, 2 keys | 23,660 |
| Standby Liveness | 2 provisioning rounds, 2 callbacks, 2 starts per callback, 2 keys | 5,448 |
| Cue Safety (also checks liveness) | 3 published cues, at most 2 sounding callbacks, then silence | 394 |

All five configurations passed with `tla2tools` v1.7.4. State counts describe
these bounds, not all possible sessions. The notice models explore all choices
of track and value for each change: no release (`0`), or either of two release
beats. This includes cancellation, rescheduling, repeated beats, ring wrap,
queue saturation and retry. Intermediate changes may be coalesced while the
queue is full; eventual delivery applies to the stable final value.

The models assume one audio producer and one provisioning consumer, fixed
track membership, and weak fairness of each process. The notice model keeps
scanning after changes stop. The cue model keeps running callbacks after its
bounded sounding phase. These scheduling assumptions are necessary for their
liveness claims. The standby model has finitely many callbacks, so publication
eventually has an opportunity to proceed.

The atomic steps abstract C++ acquire/release synchronization as coherent
publication. This is not a C++ weak-memory model. The standby feed abstracts
farbot's acquire/release as a block pin that prevents table replacement; the
cue register abstracts publication of an immutable generation/position pair.
The cue publisher may advance after the callback snapshots a request, which
conservatively permits a later cue to wait until the next callback.

Standby keys represent equality of the **whole** `StretchPrimeKey`, including
snapshot and tempo, rather than equality of release beats alone. Setup is held
constant. Priming either produces a fully initialized immutable standby or
produces none. The claim CAS and subsequent render are separate steps so the
pool can withdraw or promote between them. Pool and table references represent
the minimum owners; temporary reader copies can delay actual destruction.
Object identities are never reused. The initial active stretcher is outside
the allocation model.

Not modelled: track removal, integer counter overflow, beat/time and loop
conversion, snapshot construction, priming DSP, sample correctness, fallback
DSP, retained buffers, disk prefetch, and worker scheduling within a callback.
The cue model covers **pending pool cues**, not the voice's own `moveTo` calls
during reading. Those may legitimately move a sounding stream. Track-hold
decisions are inputs; the launcher's state machine is not modelled here.

### Checking the checks

Seven deliberately broken copies were translated and checked in temporary
directories. Each produced the expected counterexample:

| Deliberate change | Failure detected |
| --- | --- |
| Remember a notice as announced when its ring is full | `LatestDelivered` |
| Publish the write counter before writing its payload | Notice serial assertion |
| Let a voice take an already claimed standby | `ExclusiveClaim` |
| Let a voice claim without comparing its prime key | Key assertion at render |
| Publish a replacement table while the callback pins the old one | Live-object assertion |
| Omit collection of retired standbys | `RetiredFreed` |
| Apply a pending cue even when the previous callback sounded | `SoundingPreserved` |

These are validation mutations, not reported C++ bugs. A counterexample in an
unmodified model still needs a test or manual reproduction against the real
code before it counts as a defect.

### Editing

Edit the PlusCal algorithm above the generated translation, then regenerate
with the same cached jar used by `scripts/tla.sh`, for example:

```sh
python3 scripts/tla_translate.py \
    --jar "${XDG_CACHE_HOME:-$HOME/.cache}/magda/tla2tools-v1.7.4.jar" \
    specs/tla/hand_back_notices/HandBackNotices.tla
```

Keep the generated translation in the `.tla` file. The helper runs the translator
in a temporary directory, normalizes trailing whitespace, and updates the
translation checksum so formatting hooks do not cause a spurious TLC warning.
It leaves the named configs alone; they contain the intended bounds and
properties. The runner checks every `.cfg`.
