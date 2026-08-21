# The native audio engine

Epic [#1882](https://github.com/Conceptual-Machines/magda-core/issues/1882): a MAGDA-owned
audio engine that covers exactly what MAGDA uses Tracktion Engine for today, so the fork can
be removed.

It lives in `magda/engine/` as a static library, `magda_engine`, built on every configure and
linked by nothing but the tests. A configure-time check in `magda/engine/CMakeLists.txt`
refuses any file in there that includes Tracktion, or anything outside the model layer and its
own headers. Dark, but never rotting: it compiles the real model, and its tests run in every CI
job.

This document is the map. The territory is the header comments: every file in `magda/engine/`
opens with a block that explains what it is for and, more usefully, why it is shaped the way it
is. When the two disagree, the headers are right and this file is stale.

---

## 1. Where it stands

| Part | Issue | State |
| --- | --- | --- |
| Own JUCE as a direct dependency | [#1883](https://github.com/Conceptual-Machines/magda-core/issues/1883) | done |
| Break the TE to MAGDA reverse dependency | [#1884](https://github.com/Conceptual-Machines/magda-core/issues/1884) | done |
| Close the AudioEngine abstraction gap | [#1885](https://github.com/Conceptual-Machines/magda-core/issues/1885) | done |
| Device SDK seams, MagdaDevice base | [#1886](https://github.com/Conceptual-Machines/magda-core/issues/1886) | done |
| Engine-neutral device state schema | [#1887](https://github.com/Conceptual-Machines/magda-core/issues/1887) | done |
| Retire stock TE plugin wrappers | [#1888](https://github.com/Conceptual-Machines/magda-core/issues/1888) | done |
| Clip model: container from content | [#1901](https://github.com/Conceptual-Machines/magda-core/issues/1901) | done |
| **Engine core: plan, executor, PDC** | [#1889](https://github.com/Conceptual-Machines/magda-core/issues/1889) | **all 10 slices done** |
| **Arranger clip playback** | [#1890](https://github.com/Conceptual-Machines/magda-core/issues/1890) | **all 7 slices done** |
| **Parameters, modifiers, macros, automation** | [#1891](https://github.com/Conceptual-Machines/magda-core/issues/1891) | **all 8 slices done** |
| Rack graph: pins, summing, multi-out, nesting | [#1892](https://github.com/Conceptual-Machines/magda-core/issues/1892) | not started |
| External plugin hosting and hardware inserts | [#1893](https://github.com/Conceptual-Machines/magda-core/issues/1893) | not started |
| Clip launcher and session playback | [#1894](https://github.com/Conceptual-Machines/magda-core/issues/1894) | not started |
| Live input, monitoring, recording | [#1895](https://github.com/Conceptual-Machines/magda-core/issues/1895) | not started |
| Null-diff validation harness | [#1896](https://github.com/Conceptual-Machines/magda-core/issues/1896) | rig built, 3 of 8 slices done |
| Cutover: bridge rewrite, dual-engine release | [#1897](https://github.com/Conceptual-Machines/magda-core/issues/1897) | not started |

Engine core, slice by slice: plan IR and compiler ([#2010](https://github.com/Conceptual-Machines/magda-core/issues/2010)),
reference executor and value layer ([#2011](https://github.com/Conceptual-Machines/magda-core/issues/2011)),
runtime state and epoch retirement ([#2012](https://github.com/Conceptual-Machines/magda-core/issues/2012)),
latency compensation and buffer assignment ([#2013](https://github.com/Conceptual-Machines/magda-core/issues/2013)),
the plan differ ([#2014](https://github.com/Conceptual-Machines/magda-core/issues/2014)),
transport and tempo ([#2015](https://github.com/Conceptual-Machines/magda-core/issues/2015)),
disk reader and prefetch ([#2016](https://github.com/Conceptual-Machines/magda-core/issues/2016)),
offline render and taps ([#2017](https://github.com/Conceptual-Machines/magda-core/issues/2017)),
parallel executor ([#2018](https://github.com/Conceptual-Machines/magda-core/issues/2018)),
crossfading a plan swap ([#2019](https://github.com/Conceptual-Machines/magda-core/issues/2019)).

Clips, slice by slice: the snapshot ([#2034](https://github.com/Conceptual-Machines/magda-core/issues/2034)),
voices, spans and fades ([#2035](https://github.com/Conceptual-Machines/magda-core/issues/2035)),
rate conversion, looping and reverse ([#2036](https://github.com/Conceptual-Machines/magda-core/issues/2036)),
stretch and pitch ([#2037](https://github.com/Conceptual-Machines/magda-core/issues/2037)),
warp, beat detection and loop info ([#2038](https://github.com/Conceptual-Machines/magda-core/issues/2038)),
MIDI clips ([#2039](https://github.com/Conceptual-Machines/magda-core/issues/2039)),
the null-diff corpus ([#2040](https://github.com/Conceptual-Machines/magda-core/issues/2040)).

Parameters, slice by slice: the value lane and what a device reads
([#2116](https://github.com/Conceptual-Machines/magda-core/issues/2116)),
publication, addressing and the link graph
([#2117](https://github.com/Conceptual-Machines/magda-core/issues/2117)),
the automation bake ([#2118](https://github.com/Conceptual-Machines/magda-core/issues/2118)),
the LFO ([#2119](https://github.com/Conceptual-Machines/magda-core/issues/2119)),
ADSR, random and the envelope follower
([#2120](https://github.com/Conceptual-Machines/magda-core/issues/2120)),
macros at track, rack and device scope
([#2121](https://github.com/Conceptual-Machines/magda-core/issues/2121)),
value read-back taps ([#2122](https://github.com/Conceptual-Machines/magda-core/issues/2122)),
parity cases ([#2123](https://github.com/Conceptual-Machines/magda-core/issues/2123)).

Validation, slice by slice. Done: whole-project cases and the tiered oracle
([#2075](https://github.com/Conceptual-Machines/magda-core/issues/2075)),
plan goldens ([#2076](https://github.com/Conceptual-Machines/magda-core/issues/2076)),
differ property tests ([#2077](https://github.com/Conceptual-Machines/magda-core/issues/2077)),
the block-size invariance gate
([#2078](https://github.com/Conceptual-Machines/magda-core/issues/2078)).
Open: migrators ([#2079](https://github.com/Conceptual-Machines/magda-core/issues/2079)),
the DAWproject cross-check ([#2080](https://github.com/Conceptual-Machines/magda-core/issues/2080)),
the real-project corpus ([#2081](https://github.com/Conceptual-Machines/magda-core/issues/2081)),
the parity envelope suite ([#2082](https://github.com/Conceptual-Machines/magda-core/issues/2082)).
Parity cases for a feature live with the feature, so #1891 through #1895 each carry their own,
the way #2040 was the last slice of #1890.

---

## 2. The one idea

An audio callback may not wait, may not allocate and may not free. Everything else follows from
arranging the engine so it never has to.

So nothing the audio thread reads is ever edited. Four separate immutable values are compiled
off the audio thread and swapped in whole, and the audio thread only ever reads the current
one. They are separate because they change at wildly different rates, and lumping them together
would make a fader move cost what a device move costs.

```mermaid
flowchart LR
    subgraph model["the model, edited on the message thread"]
        T["tracks, devices, routing"]
        C["clips"]
        V["faders, sends, mutes"]
        M["tempo, loop, locates"]
    end

    subgraph compiled["compiled off the audio thread"]
        PC["PlanCompiler"]
        CC["ClipSnapshotCompiler"]
        VR["value resolver"]
        TC["TransportClock and TempoMap"]
    end

    subgraph live["published, read on the audio thread"]
        P["RenderPlan"]
        S["ClipSnapshot"]
        PV["PlanValues"]
        TS["TransportSnapshot"]
    end

    T --> PC --> P
    C --> CC --> S
    V --> VR --> PV
    M --> TC --> TS

    P --> EX["executor"]
    S --> EX
    PV --> EX
    TS --> EX
    EX --> OUT["audio device"]
```

What travels on which channel, and what it costs:

| Channel | Carries | Changes when | Cost of a change |
| --- | --- | --- | --- |
| `RenderPlan` | signal topology only | a device moves, a track is added, a chain is bypassed | a compile, at human speed |
| `PlanValues` | gains, faders, pans, sends, mutes | a mixer move | resolve a flat table, no compile |
| `ClipSnapshot` | what every track plays, resolved | a clip moves, is trimmed, is faded | recompile the snapshot, no plan touched |
| `TransportSnapshot` | tempo, loop, metronome, and where the transport has been asked to be | a tempo edit, a loop drag, a locate | published like the others |

The transport row is the one to read carefully, because its name invites the wrong reading. The
snapshot is the clock's **input**, not a per-block reading of it: where the timeline actually is
belongs to `TransportClock::advance`, which owns that cursor on the audio thread and exposes the
position separately. Nothing is published every block.

The rule of thumb, and the one worth remembering: **moving a clip or a fader never recompiles a
plan.** If a change to the model forces a plan recompile, either it really is topology or
something has been put in the wrong channel.

---

## 3. What a plan is

A flat, dependency-ordered list of ops. Not a graph of objects: a vector you walk from top to
bottom, where every op names the ops it reads from by index. That is what makes it dumpable,
diffable and schedulable.

Here is a real one, from the compiler's golden test: one track, one device, into the master.

```
magda-render-plan v1
ops=13 outputs=1
[  0] ClipAudio   det   T1:clipAudio                   in=-                out=audio       deps=0
[  1] MixAudio    det   T1:trackAudioInput             in=0:0              out=audio       deps=1
[  2] Device      det   T1/D7:deviceProcess            in=1:0,-,-          out=audio       deps=1
[  3] Gain        det   T1/D7:deviceGain               in=2:0              out=audio       deps=1
[  4] Meter       det   T1/D7:deviceMeter              in=3:0              out=audio       deps=1
[  5] Fader       det   T1:trackFader                  in=4:0,-            out=audio       deps=1
[  6] Meter       det   T1:trackMeter                  in=5:0              out=audio       deps=1
[  7] Gain        det   T1:trackMute                   in=6:0              out=audio       deps=1
[  8] MixAudio    det   T-2:trackAudioInput            in=7:0              out=audio       deps=1
[  9] Fader       det   T-2:trackFader                 in=8:0,-            out=audio       deps=1
[ 10] Meter       det   T-2:trackMeter                 in=9:0              out=audio       deps=1
[ 11] Gain        det   T-2:trackMute                  in=10:0             out=audio       deps=1
[ 12] Output      det   T-2:hardwareOutput             in=11:0             out=-           deps=1
ready=0
```

`T-2` is the master. `det` is the op's liveness domain: whether what it computes is the same
every time it is asked, or depends on something live arriving. It is read today, in two places.
`validatePlan` checks it both ways, so a deterministic op can never read a live one and a live
op can never appear without a live source behind it, and the crossfade pass refuses to introduce
a live producer ahead of a deterministic consumer. Its larger purpose is still ahead of it: the
anticipative executor ([#1898](https://github.com/Conceptual-Machines/magda-core/issues/1898))
is what the tag was carried from day one for.

```mermaid
flowchart TD
    A["0 ClipAudio"] --> B["1 MixAudio<br/>track input"]
    B --> C["2 Device<br/>the compressor"]
    C --> D["3 Gain<br/>device trim"]
    D --> E["4 Meter<br/>device tap"]
    E --> F["5 Fader<br/>track volume and pan"]
    F --> G["6 Meter<br/>track tap"]
    G --> H["7 Gain<br/>mute and solo"]
    H --> I["8 MixAudio<br/>master input"]
    I --> J["9 Fader<br/>master"]
    J --> K["10 Meter"]
    K --> L["11 Gain<br/>master mute"]
    L --> M["12 Output"]
```

Two things that look like clutter and are not:

**An ordinary device is three ops.** Its processing, its gain trim and its meter tap are
separate, so the differ can carry, rebuild and crossfade each independently. A device whose trim
changed does not rebuild.

Three is the usual shape rather than a rule: an analysis device is a transparent passthrough
with no trim and no tap, so it compiles to a bare process op, and a compile with device meters
switched off emits no meter ops at all.

**Every op has a key.** `T1/D7:deviceGain` is the model location plus the structural role, and
`validatePlan` proves keys are unique. That key is how a new plan recognises an op in the old
one, which is the whole of section 4.

The op vocabulary is deliberately small: `ClipAudio`, `ClipMidi`, `AudioInput`, `MidiInput`,
`Device`, `MixAudio`, `MergeMidi`, `Delay`, `Crossfade`, `Gain`, `Fader`, `SendTap`, `Meter`,
`Output`.

---

## 4. The life of an edit

```mermaid
sequenceDiagram
    participant U as an edit
    participant P as publishing thread
    participant S as RuntimeStateStore
    participant A as audio thread

    U->>P: the model changed
    P->>P: compile a new plan
    P->>P: validate it
    P->>P: diff it against the live one
    P->>S: bind ops to instances
    S-->>P: devices, clip sources, inputs, meter taps, kept by model id
    P->>P: prepare: resolve delays, assign buffers
    P->>A: publish
    A-->>P: the block in flight finishes
    Note over A: the next block renders the new plan
    P->>P: destroy the retired epoch here
```

The parts that matter:

**The differ** (`plan/PlanDiff.hpp`) decides what survives. An op carries its state when the key,
the kind, the ports and the connected inputs all agree. That is what keeps delay lines full and
tails alive across an edit, and it is the answer to the rebuild click.

**The store** (`exec/RuntimeStateStore.hpp`) owns the expensive things an op resolves to:
devices, a track's clip sources, live inputs, meter taps. Keyed by model id rather than by plan
membership. A bypassed device contributes no ops at all, so keying on the plan would tear its
plugin down and rebuild it when you toggle bypass, losing tail, state and load time on a gesture
that should be free. Plan-named means playing; model-named means kept; only deletion from the
model destroys anything.

File readers are **not** here, and that is deliberate rather than an omission. A clip's readers
are opened and owned by `ClipVoicePool`, on its own thread, and reach the audio thread through
their own table; they never enter a plan epoch. Section 6 is where they live.

**Prepare** (`exec/PlanLayout.hpp`) is what a plan becomes when it meets the instances behind it.
How many samples each delay holds comes from what a loaded plugin reports, which the model
cannot know and the compiler never saw. A plugin whose latency changes therefore re-prepares
rather than recompiling.

**Publish** blocks the publishing thread until the audio thread finishes the block it is in.
That is the point: after it returns, nothing the audio thread can reach names the old epoch, so
the old epoch is destroyed right there, on the publishing thread. The callback never frees.

**Crossfade** (`plan/PlanCrossfade.hpp`) ramps the edges an edit moved. Fades are ops the
compiler never emitted, added by a pass over its output and gone again at the next publish.

---

## 5. Who runs what

```mermaid
flowchart TB
    subgraph audio["audio thread"]
        A1["execute the plan"]
        A2["read the four published values"]
        A3["copy out of prefetched chunks"]
    end

    subgraph pub["publishing thread"]
        P1["compile, diff, prepare"]
        P2["publish, then destroy what was retired"]
    end

    subgraph pool["render thread pool"]
        W["ops in dependency order, summing in compiled order"]
    end

    subgraph voices["clip voice thread, 10 ms rounds"]
        V1["open the files clips will need"]
        V2["cue their readers, retire the passed ones"]
    end

    subgraph disk["prefetch thread"]
        D1["fill chunks ahead of the callback"]
    end

    P2 -->|"swap"| A2
    V2 -->|"a table of readers"| A2
    D1 -->|"chunks"| A3
    A1 --> W
```

The audio thread never allocates, never locks and never frees. Every other thread here exists so
that stays true.

The parallel executor deserves one line of its own: its output is **bit-identical** to the
single-threaded reference executor at every thread count, because everything that sums does so
in compiled order rather than in the order its inputs finished, and buffers are shared on a test
over the dependency graph rather than over the op list. The schedule costs nothing at run time,
because the dependency counts were baked into the plan when it was compiled.

---

## 6. How a clip plays

```mermaid
flowchart LR
    CM["clip model"] --> CC["ClipSnapshotCompiler"]
    CC --> SN["ClipSnapshot<br/>spans, holes, fades, resolved"]

    SN --> POOL["ClipVoicePool<br/>off the audio thread"]
    SN --> SRC["ClipAudioSource<br/>on the audio thread"]

    POOL -->|"opens files a second ahead"| ST["PrefetchStream per event"]
    POOL -->|"publishes"| TBL["ClipStreamTable"]
    TBL --> SRC
    PT["prefetch thread"] -->|"fills"| ST
    ST --> SRC

    SRC --> VO["up to 16 ClipVoices"]
    VO --> OUT["the track's buffer"]
```

The snapshot is **resolved**: occlusion, crossfades and takes are worked out once, at compile
time, by the same model functions the UI draws with (`computeAudibleSpans` in
`core/ClipOcclusion.hpp`, `effectiveFadesIn` in `core/ClipFades.hpp`). A voice therefore plays a span with fades and
never looks at its neighbours. Two clips crossfading are two voices each playing the fade it was
handed, and nothing downstream pairs them up.

The pool runs a second ahead of the transport (`kCueAheadSeconds`), opening files and pointing
readers at the sample their clip starts on. That is the difference between a clip starting on
the beat and a clip starting a block late: a read that does not continue the last one is a seek,
and a seek costs a block of silence.

Two ceilings, and they answer different questions through different counters.

`kMaxVoicesPerTrack` is 16, and it is how many clips a track can sound in one callback. Past it,
the pool reports `overSubscribed`, which is peak concurrency beyond the ceiling, and the source
reports `starvedVoices` as the clips actually go unheard.

`kMaxReadersPerTrack` is twice that, because a reader has to exist before its clip is due. Past
it, the counter is `unbridged`, and `overSubscribed` stays at zero by design: a lane of
sequential slices fills the reader budget many times over without two of them ever sounding
together. A crowded one-second window is normally harmless, so only clips the budget turned away
that start within `kReadAheadBridgeSeconds`, which is to say clips that will be due before the
next round, count as unbridged.

None of the three is silent about it, because silence nobody counted is indistinguishable from a
gap in the material.

### The reading chain

Reverse, looping and rate conversion are not processing. They are which of a file's samples
answer a position, so each is a reader wrapped around the reader, built once when the pool opens
a clip.

```mermaid
flowchart LR
    F["the file on disk"] --> R["ReversedAudioFileReader<br/>only if the clip plays backwards"]
    R --> L["LoopingAudioFileReader<br/>only if the clip loops"]
    L --> S["ResamplingAudioFileReader<br/>only if the rates differ"]
    S --> PS["PrefetchStream"]
    PS --> ST["ClipStretcher<br/>only if the clip is not at its file's speed"]
    ST --> V["ClipVoice<br/>span, holes, fades, channels, gain, pan"]
```

Everything above the chain sees one forward file at the device's rate, whatever the clip is set
to. A clip that asks for none of it reads through no extra layer at all.

### Speed and pitch

Slice 4 ([#2037](https://github.com/Conceptual-Machines/magda-core/issues/2037)). The chain
above decides **what** the reading holds; this decides **how fast it is consumed**, which is why
it sits above the stream where the chain sits below it.

All of it is one function. `readingPositionAt` in `clip/EventPlacement.hpp` says where in the
reading a moment of the timeline sits, and everything on the list is that function answering
differently:

| What the clip asks for | What the position does |
| --- | --- |
| its file's own speed | advances one reading sample per output sample |
| a speed ratio | advances by the ratio, a constant, resolved in the snapshot |
| auto tempo | advances by the beats that have passed, times a beat of the file |
| analog pitch | advances by the ratio the model already folded the pitch into, with no stretcher |
| a speed ramp fade | the moment itself is warped near the clip's edge, and the rate with it |
| warp markers | advances through a compiled map whose rate changes at every marker |

Auto tempo is the one worth reading twice. Its ratio is the project's tempo over the file's own
and moves with the tempo curve, so it cannot be resolved to seconds ahead of a block. What saves
it is that the integral of that ratio is beats: the material an instant has consumed is how many
beats have passed since the event began, times what a beat of the file is worth. That is why the
clock publishes both faces of one instant, and why there is no second tempo map on the audio
thread.

### Warp

Slice 5 ([#2038](https://github.com/Conceptual-Machines/magda-core/issues/2038)), and it added
no machinery at all below the position map: the voice, the stretchers and the reading chain are
untouched by it. A block already asks `readingPositionAt` at both its ends and hands the
difference to the stretcher, so a ratio that changes at every marker costs nothing that a moving
auto-tempo ratio did not already cost.

The model holds warp as `(sourceTime, warpTime)` pairs, piecewise linear, slope 1 outside the
marker range. That direction answers where a bit of file lands musically, which is what the
editors ask. Playback asks the inverse, so `clip/WarpMap.hpp` compiles one: sorted, strictly
increasing on both sides, and with whatever could not be part of a monotonic map dropped at
compile time with a diagnostic rather than divided by on the audio thread.

Three things compose with it, and each is decided in one place:

- **Reverse** stays a coordinate change, as it is everywhere else in this layer. The map is not
  mirrored; a reversed event walks it backwards from the far end of what it reads and mirrors the
  answer. Mirroring the map instead would need the length of the region the event reads, which is
  itself an answer from the map. The incumbent cannot do reverse and warp together at all -- it
  bakes warp into a rendered proxy file and can only bake one thing per clip, so
  `WaveAudioClip::createRenderJob` returns the reverse job and the markers are silently lost.
- **Looping** is the one case where the reading chain cannot do its own tiling. Folding below the
  stream works because the reading advances linearly, and under warp it does not: a position that
  had already been through the map would fold in the wrong domain and every pass after the first
  would play straight. So a warped loop folds in warp time, above the map, and the tiling below is
  switched off. The reading then saws back at each wrap rather than climbing, which costs one seek
  per pass.
- **Stretcher sizing** reads the map's steepest segment rather than its average. A warped event
  has no single rate, and the pre-roll has to cover the fastest stretch of it.

Where the markers come from when the user has not placed them is the other half of the slice.
`analysis/TransientDetector.hpp` is the incumbent's detector reproduced coefficient for
coefficient -- envelope followers, a differentiator, a threshold from the sensitivity, a spacing
rule -- because a detector that found different transients would move every auto-detected marker
in every project that already has one. `io/SourceLoopInfo.hpp` is the third piece and is not an
analysis at all: a file's own tempo and beat count are an acid chunk that JUCE already parses, so
what the model seeds its interpretation from is a parse over a metadata map, testable without a
file.

A block then reads exactly `round(P(end)) - round(P(start))` samples and hands them to the
stretcher to come back as the block's own length, so the ratio a block runs at is whatever its
own two ends say. A tempo curve and a speed ramp therefore cost nothing extra, and nothing
accumulates: both ends are rounded rather than counted forward, so one block's reading ends
exactly where the next one's begins.

**Where a stretcher lives** answers the questions that come with it. One per provisioned event,
built and configured by `ClipVoicePool` on the thread that opens the file, carried to the
callback in the same table as the stream (`clip/ClipStreamFeed.hpp`). So a plan swap does nothing
to it, because it never enters a plan epoch; a loop wrap does nothing to it, because tiling
happens below the stream and a wrap is a discontinuity in the material rather than a change of
position; and a locate resets and re-primes something that already exists, which allocates
nothing. An event that asks for no stretch gets none, the same rule the reading chain follows.

**Latency is answered here rather than reported upwards.** Every engine wants material from
*before* the first sample to be heard, says how much, and the pool cues the stream that far back,
so a voice's first read is one contiguous read that begins with the priming samples. A
`ClipAudio` op therefore reports no latency at all and stretched voices stay aligned with
unstretched ones on the same track. One deliberate divergence from the fork: its Signalsmith
wrapper primes with the material *at* the start rather than before it, so it begins every
stretched clip about a window late, and the engine is the one that is right. The corpus pins
that offset rather than tolerating it: measured by cross correlation, and required to equal the
stretcher's own reported priming latency scaled by the ratio it runs at. A shift nobody
predicted is a clip in the wrong place, and the case is refused rather than aligned.

The engines are `third_party/signalsmith-stretch` (MIT, the default, and what the pinned mode
`kSignalsmith` names) and `third_party/soundtouch` (LGPL-2.1, its own replaceable static target,
carried because `kSoundTouchNormal` and `kSoundTouchBetter` are project-file integers and
sessions saved with them have to play as they were made). A clip that resamples instead of
stretching uses the same cubic curve the rate converter below the stream uses, so a file at
another rate and a clip playing fast are not two different sounds.

### MIDI clips

Slice 6 ([#2039](https://github.com/Conceptual-Machines/magda-core/issues/2039)). Audio and MIDI
share the snapshot, the span and the interior silences, and share nothing below that:
`clip/ClipMidiSource.hpp` has no file, no reader, no stretcher and no pool. What it has instead
is a question audio never has to answer.

**The invariant.** A note-off is never emitted for a note the source did not start, and never
withheld from one it did. `clip/ActiveNoteList.hpp` is what makes that structural rather than
careful, and it outlives every clip, block and plan that passes through the source because a
note does too. Five things end a note and only the first falls out of the material: a loop pass
running out or a clip's span ending; a locate or a wrap; a stop; a snapshot swap that moved or
deleted what was sounding; and destruction, which needs nothing, because a source dies with its
track and its output port goes with it.

**Compiled, not carried.** `clip/MidiEventList.hpp` is one sorted array of short messages in
content beats. The curve densification, the MPE channel assignment, the same-pitch overlap rule
and the two offsets resolve once, off the audio thread, exactly as an event's warp markers do.
The fork arrives at the same place by a longer road, building a playback sequence per clip; the
difference is that its sequence lives inside `te::MidiClip`, so editing a curve rebuilds it, the
TreeWatcher sees the tree change and playback restarts under a rolling transport.

**A loop is a coordinate change, not a copy.** The fork unrolls, writing one copy of the
sequence per repetition. A block here is a beat range, and folding it through the loop gives a
handful of sub-ranges over the one list. The per-pass clipping rule is kept exactly, because it
is what puts every note's off in the same pass as its on. Nothing reads a loop as a length
either, so `MidiClip::disableLooping` truncating a clip to one loop length has no counterpart
here: the span is the length.

**Groove is the one thing not resolved at compile time**, and it cannot be. It is anchored to
the project grid, so a clip whose loop length is not a whole multiple of the template's period
grooves each pass differently, which is what the fork delivers by re-timing its sequence to
`clipRange.start + loopIndex * loopLength` once per pass and grooving it there. So the table is
compiled with the clip's strength folded in and the lookup runs per pass, at emit time. The
block's event search widens by the table's own worst-case displacement, which it knows exactly.

**The chase is exact rather than nearly right**, and that follows from how curves densify.
Locating leaves every controller at the value its curve is at, which is the last event before
the instant. Because a message is emitted only when the quantised value changes, nothing emitted
since means nothing changed since, so the last event *is* the current value. Under a fixed grid
it would be up to a grid step stale and the synth would sit on the stale value until the next
point arrived.

**Two deliberate divergences**, beside the Signalsmith priming one above.

Controller density is the larger. Messages go out on every change of the quantised value and no
closer together than about a millisecond, rather than on the sync layer's 1/16-beat grid. That
grid is anchored to the wrong axis, which makes it both too dense and too sparse: a ramp of one
unit over eight bars is 512 near-identical messages on it and two here, while a pitch-bend dive
over a hundred milliseconds gets three grid points at 120 BPM and about a hundred here. It also
moves with tempo, running at 8 Hz at 30 BPM and 128 Hz at 480 for the same drawn curve, when
smoothness is a wall-clock property. The floor is what bounds the cost, and it bounds it where
the 1/16 grid was aimed: about ten messages per block per controller, in wall-clock rather than
in beats, so #1193 stays shut. Because this alters the controller stream feeding an arbitrary
synth, the null-diff corpus grows a channel rather than a tolerance: MIDI event streams are
compared as their own artifact, where the difference is exact and event-addressable, and
curve-driven audio comes out of the near-null audio assertion instead of loosening it.

The smaller: `midiOffset` applies to a clip that does not loop. The fork's arranger path drops it
there while its session path applies it, which is a gap in the sync layer rather than a semantic.

---

## 7. How parity is checked

Six of the clip slices were judged by tests written beside them, and a test asserts what its
author believed the rule was. The corpus
([#2040](https://github.com/Conceptual-Machines/magda-core/issues/2040)) is the one thing here
that cannot: thirty-eight projects, each built as model values and handed to both engines,
neither of which gets a say in what the other produces. It lives in `tests/NullDiff*` and runs in
`magda_juce_tests`, because the incumbent leg is a `te::Edit`. The canonical report it prints
names the count, so `cases=38` at the top of a run is the figure this paragraph has to match.

**Nothing is golden.** A checked-in reference render would freeze the fork at the moment it was
recorded, so the day the fork changes the corpus would report an engine that broke. Both legs
render on every run.

**The material is chosen per case, never the tolerance.** Where the engines must agree sample
for sample, which is placement, trims, fades, loop tiling, reverse and comping, the material is
impulses and steps and the floor is -120 dBFS peak. Where an interpolator or a stretcher stands
between them, the material is band limited well below Nyquist, so interpolation error falls far
below the floor while a wrong position or a dropped sample stays as loud as it was. The rule the
calibration run established: change the shape of the comparison, never the size of the
allowance.

**Three divergences are pinned as numbers with their mechanisms**, rather than absorbed:
Signalsmith priming above, controller density in section 6, and `midiOffset` on an unlooped
arranger clip. Each is asserted at exactly the size it was declared to be, so the day one of
them changes, a case fails.

**The maps are compared as functions, not through a render.** `TempoMap::beatToTime` against
`te::TempoSequence` and `WarpMap::sourceSecondsAt` against `WarpTimeManager`, sampled densely and
asserted to the microsecond, before any case renders. A mapping disagreement then fails once
with a number instead of once per case as a waveform.

The corpus found five engine bugs on its first runs, all of them invisible to the slice tests
that passed: an offline render that provisioned no readers and produced silence, a launch ramp
clamped to the first block, that same ramp firing on a voice with nothing before it to be
discontinuous with, a rate resolved from a block's own two ends, and an MPE note opened with
neither timbre nor pressure.

It also falsified a prediction. The stretch cases do not null after their shift: priming sets a
vocoder's initial phase, phase in a vocoder is memory, so two differently primed stretchers never
reconverge even on identical libraries. Those cases are judged on envelope timing within a
sample and a magnitude bound with its window and hop stated, which is what survives framing.

**What a case declares** is two independent things, because they are two independent questions
and a project answers both. The audio tier is a determinism class rather than a per-case
preference: it follows from what stands between the two engines on that case's paths. Whether
the captured MIDI streams are compared is its own flag, so a project with an instrument track
and audio tracks asserts both.

`project.mixed` is the case that does it: an audio track first, two instrument tracks behind it,
judged as `Exact` audio and as a MIDI stream at once. It exists because the redesign is otherwise
unexercised. A capture placed on a track that is not the first, more than one capture aggregated,
and a verdict that is the audio and the MIDI together could all have regressed with every case
still green, since every other MIDI case sets the audio tier to `None` and every audio case
leaves the flag off.

| Tier | What it asserts | Where it applies |
| --- | --- | --- |
| `None` | nothing; the MIDI comparison carries the case | the five `midi.*` |
| `Exact` | residual under the floor, nothing allowed for | deterministic DSP, routing, the mixer |
| `Aligned` | one declared offset, undone, then `Exact` | anything whose whole effect is a delay |
| `Spectral` | pinned shift, envelope timing, magnitude bound | a phase vocoder in the path |
| `Invariants` | finite, equal length, bounded step, decayed tail | a plugin that owes nobody a sample |
| `Measured` | measured and printed, asserted only to be finite | `stretch.broadband` |

`Aligned` and `Invariants` have no corpus case yet, and they are not equally ready.
`Invariants` is implemented and tested against known-bad pairs: a NaN, a length difference, a
step past the bound and a tail that never decays are each fed to it and each has to be reported.
It is what [#1893](https://github.com/Conceptual-Machines/magda-core/issues/1893) needs, since an
external plugin has no null to give, and its checks are also what
[#2077](https://github.com/Conceptual-Machines/magda-core/issues/2077) asserts outside a changed
graph region. `Aligned` is one line of judgement over two pieces that are tested separately, the
fractional alignment and the null, but nothing yet constructs a case in that tier, so the
runner's branch for it has never executed. The declaration rule is asserted where it will bite
first: the corpus-shape tests refuse a case that names a tier without the figure that tier needs,
so the day somebody writes an `Aligned` case with no offset, or an `Invariants` case with no
discontinuity bound, that is a failure at declaration rather than a comparison that quietly
allowed anything.

The issue's fifth class, scripted interactive checks, is deliberately not a tier. A launcher or
a monitoring case is not an offline render of a range, so it needs a different runner rather
than a different verdict.

**The mixer** is where the corpus first has more than one track. `resolvePlanValues` implements
the fader law, the linear pan law, mute inheritance and solo through destination routing, and
until [#2075](https://github.com/Conceptual-Machines/magda-core/issues/2075) none of it had been
compared against the incumbent at all. Eight `mix.*` cases now do: summing, the fader across its
range, the bottom of that range on its own, the fader past both ends where the clamp is, the pan
law at its ends, mute, solo, and the master's own fader and pan. The incumbent leg drives them
through the same four calls `AudioBridge::trackPropertyChanged` makes.

Only `mix.summing` has more than three tracks, which used to be a constraint rather than a
preference. Four
audio tracks in one Edit collide in the fork's node-identity hash, so the graph's uniqueness
assertion fires; it reproduces on four tracks carrying one impulse clip each, which is nothing to
do with the mixer. The runner refuses to certify a case that provoked an assertion, so this is a
failure rather than a quiet pass, and the collision is
[#2085](https://github.com/Conceptual-Machines/magda-core/issues/2085).

Sends are the one routing dimension left out, and not because they are hard to model: the
compiler emits `SendTap` pre and post fader and the value layer resolves the levels already.
The incumbent's sends live on `te::AuxSendPlugin` instances that `PluginManagerSync` creates,
which wants a `PluginManager` and the device layer behind it, and writing those plugins straight
into the leg would be the second sync the corpus refuses to have. They belong with the rest of
the routing graph, in [#1892](https://github.com/Conceptual-Machines/magda-core/issues/1892).

**Parameters are where the corpus first runs a device.** Until
[#2123](https://github.com/Conceptual-Machines/magda-core/issues/2123) a `Device` op resolved to
a stand-in and the incumbent instantiated none of the model's devices, so a parameter nothing
reads was a parameter nothing could compare. That is now a gain with one parameter, written once
as a contract (`tests/NullDiffGain.hpp`) and implemented in both legs the way the MIDI capture
already was. What a gain device renders is the value of its own parameter, so a case that plays a
constant into it draws the curve directly.

Nine cases, all nulling: the stored value, a step curve over it, a square LFO over it, both at
once, the stored value under each, a macro at track and at device scope, and the fader past both
ends of its range where the clamp is. Each drives the incumbent through the app's own paths
rather than a second set written here: the curve is emitted by the same `bakeLaneIntoCurve` the
playback engine uses, and the modifiers and macros are built by `ModifierSyncWalker`, the walker
`PluginManager` drives.

**The steps land on the half beat and the impulses land on the beat**, which is why these compare
at the ordinary floor. Both engines settle a parameter at the top of a block, so they agree
wherever a curve is holding still and can differ by up to a block wherever it jumps; eleven
thousand samples of silence either side of every jump covers a block at 4096 as well as at 512,
so these cases are also bit identical across the invariance gate rather than exempt from it.

**Three things are named rather than covered**, each after being tried. Rack-scope macros need a
`te::RackType` that `RackSyncManager` builds out of a `PluginManager`, which is the boundary sends
stop at and moves with #1892. The random walk cannot be nulled by anybody, since the fork seeds
its generator from the clock. The envelope follower is fed by a `FollowerSourceTapPlugin` that
`PluginManager` installs, so without the device layer the fork's follower is handed nothing.

The envelope is a finding rather than a boundary. Its note gate is behind that same device layer,
and its transport gate is the fork asking `TransportControl::isPlaying()`, which is false
throughout every offline render: a transport-triggered envelope does nothing in a bounce in the
current engine, while the engine being built plays it. The case was written and renders 0.625
against silence. It is not in the corpus, for the reason reverse-plus-warp is not.

The slice also found one in the fork's LFO. `std::fmod` keeps the sign of its left-hand side, so
a negative edit time gave `Ramp::setPosition` a negative position, which it asserts on and then
clamps to zero. A count-in produces one, and so does the lead-in an offline render primes the
graph with, which is half a second of it on every bounce: forty-four assertions per case and a
phase pinned at the top of its cycle throughout. The fix wraps forward instead.

**Properties over generated edits**
([#2077](https://github.com/Conceptual-Machines/magda-core/issues/2077)) are the same argument
turned on the differ. The corpus compares two engines over projects somebody wrote down; this
generates the projects instead, because the differ's inputs are pairs of plans and the pairs that
matter are the ones nobody thought to write. A vocabulary of the edits a user can perform, a
seeded generator that strings them together against the project as it stands, and every edit
addressed by the id of what it touches rather than by where that sits, so any subsequence of a
failing sequence still runs and a failure is shrunk to the edits that caused it before it is
printed. It lives in `tests/PlanEdit*` and runs in `magda_tests`, since none of it needs a
`te::Edit`.

Four properties. **Carry** is checked both ways against a signature built from the two plans
alone, so a differ that carried nothing fails it as surely as one that carried too much, and the
runtime half of the same decision is predicted from the plans and their layouts and required to
match the delay lines and fade ramps the executor says it adopted. **Retirement** is that
`carriedFrom` is a partial injection whose complement is exactly `retired`, and that the store
destroys an instance only once neither the live plan nor the model names it. **Alignment** is
that every fan-in has all its inputs arriving at one latency with at least one of them waiting
for nothing, and that a track no edit reached reports the latency it did before. **The null** is
the one that catches the most: every track carries an analysis device that keeps what passed
through it, the sequence is rendered once whole and once with every edit that cannot reach a
chosen track removed, and the two recordings of that track have to be equal sample for sample,
across a different number of swaps and a different amount of state carried over them.

What is asserted about the track an edit does change is a bound and a destination rather than a
null. No sample may move by more than a fade's worth of the transient in one step where the pass
reported it faded every edge it moved, and once the transient is over the session has to render
exactly what a session that had just opened the same project renders: carried state may decide
how the signal got there, never where it arrives. Where an edge was refused a fade, or a delay
line anywhere upstream starts flushed, the step is a declared divergence and the bound does not
apply, which the property reads off the pass's own report rather than guessing.

That exemption is also how the bound could quietly stop existing, so how often it is reached is
counted rather than assumed, over the tracks the edit could reach and among those the ones where
something moved: a track the edit cannot touch renders the constant it rendered before, and a
flat window meets any bound at all. The deep sweep measures 7111 bounded transients on reached
tracks, 1647 of them moving, against 10808 exempt, and the ordinary run asserts all three counts
are non-zero.

The ordinary run sweeps a few hundred sequences of fourteen edits in about eight seconds. The
deep sweep behind `./magda_tests "[deep]"` is four thousand sequences of forty and takes eight
minutes; it is what to reach for when the differ or the crossfade pass changes. Both were checked
against a broken engine before being believed: carrying an op whose inputs had moved, never
carrying a delay line, one sample too much compensation on every edge, and a fade taking a
changed op as its old side are each caught, three of them as a sequence of one or two edits, and
so are a store that leaks an instance nothing names and a device that reports more latency than
it delays by. Neither sweep has found an engine bug. What they found instead, three times, was
the harness: twice calling an edit unrelated when it was not, and once letting a generated device
claim a latency its own ring could not honour, which would have left every render measuring a
graph that was misaligned in fact while every latency assertion above it passed.

**The block-size invariance gate**
([#2078](https://github.com/Conceptual-Machines/magda-core/issues/2078)) asks one question of the
corpus, and it is the question `RenderContext` already answers on paper: output is a function of
timeline position and nothing else, so a project renders the same audio however the callback is
cut up. Every non-MIDI case is rendered at 64, 96, 512 and 4096 samples and the other three are
compared against 512. It lives in `tests/test_null_diff_block_size.cpp` and runs in `magda_tests`,
because the claim is the native engine's alone: the incumbent owes nobody block-size invariance
and could not be held to it here if it did.

4096 is the size that carries the guarantee a user cashes in, which is a large-buffer bounce that
sounds like the small-buffer one. 64 is where anything that assumed it had room to work fails. 96
is not a power of two on purpose: a voice drives a stretcher in 128-sample cells anchored to where
its event begins, so at 512 and 4096 every block boundary is also a cell boundary and the holdover
buffer, the head-drop and the mid-cell resume never run for a clip that starts at zero, while 96
rotates through every phase the cell grid has.

A project of internal devices is held to bit identity rather than to a floor, since there is no
mechanism by which a deterministic graph fed the same timeline produces anything else and a floor
would let a real dependence hide under it until it grew. A project hosting an external plugin is
compared within an epsilon it declares, with the plugins named beside the residual; the epsilon is
refused outright on a project with no plugin in it, so it can only ever be bought by something
there is to attribute the difference to. Nothing hosts a plugin yet, so every corpus case is on the
strict side today and the external path is covered by hand-built cases, which is deliberate:
[#1893](https://github.com/Conceptual-Machines/magda-core/issues/1893) should find a gate to land
in rather than a gate to widen.

It has paid for itself four times. Three fixes came out of the first run at two sizes: a rate that
varies within a block resolved from that block's own two ends, so a speed ramp, auto tempo across a
tempo change and a warped clip approximated their curve differently at every block size; a
stretcher that framed whatever sizes it was handed; and a cell reading past its block's end taking
its beats from a straight line through that block rather than from the tempo map. Adding 4096 found
the fourth on the first run, in a path that had been green at 64, 96 and 512 all along:
`SoundTouchClipStretcher::preRollSamples` sized its cushion from the block, so the stretcher was
primed with 4096 samples of surplus at 4096 where it had 512 at 512. Priming is what sets a phase
vocoder's state, so the two renders of the same clip came out a decibel apart. The cushion is a
cell now, which is the unit the voice actually drives it in.

What the rig still does not cover is the rest of
[#1896](https://github.com/Conceptual-Machines/magda-core/issues/1896): no case carries a device
either engine ships, because nothing yet runs one of those under both of them, and the gain
device the parameter cases play through was written for the corpus; the interactive paths have no
runner; and none of the migration half exists.

---

## 8. What is not built yet

Worth knowing before reading the code and wondering where something is:

- **Racks, the rest of them** ([#1892](https://github.com/Conceptual-Machines/magda-core/issues/1892)).
  The ordinary shape already compiles: `PlanCompiler::emitRack` emits chain faders, the rack
  mix, its MIDI merges and its output fader, nested racks included, and the compiler and
  executor tests cover them. What is missing is the full pin graph, auxiliary and multi-output
  routing, and parity for what those imply.
- **External plugins** ([#1893](https://github.com/Conceptual-Machines/magda-core/issues/1893)).
  A `Device` op resolves to whatever the host hands the store. Nothing hosts VST3 yet.
- **Launcher and recording** ([#1894](https://github.com/Conceptual-Machines/magda-core/issues/1894),
  [#1895](https://github.com/Conceptual-Machines/magda-core/issues/1895)).
- **Wiring the ports to the model.** The native transient detector, the loop-info parse and the
  groove template all exist and are tested; `WarpMarkerManager`, the clip model and the clip
  inspector still get theirs from Tracktion, and `compileClipSnapshot` takes an empty
  `GrooveTemplateSet` until something fills it. Swapping them over belongs with switching the
  engine on rather than with implementing it.
- **The rest of the validation harness**
  ([#1896](https://github.com/Conceptual-Machines/magda-core/issues/1896)). The rig exists and
  section 7 describes it, whole-project cases and the tiers above them (#2075), the plan goldens
  (#2076), the differ properties (#2077) and the block-size gate (#2078) included. Missing: the
  migrators (#2079), the DAWproject cross-check (#2080), a real-project corpus (#2081), and the
  parity envelope suite that gates cutover (#2082).

---

## 9. Where the code lives

| Directory | What is in it |
| --- | --- |
| `plan/` | the IR, the compiler, the differ, the crossfade pass, the canonical dump |
| `exec/` | the two executors, the value table, the layout pass, the runtime store, the session, offline render |
| `clip/` | the clip snapshot and its compiler, the voice pool, the voices, the MIDI source, the feeds |
| `io/` | file readers, the prefetch stream and its thread, the reading chain, the placement mapping |
| `transport/` | the tempo map, the sample clock, the metronome |
| `tap/` | what a meter writes and the UI reads |

Every one of those files opens with a comment explaining why it exists. Read that before the
code; most of them answer the question you are about to ask.

---

## 10. Building and testing it

The engine target is on by default, so a normal build already builds it:

```
make debug
make test
```

Its tests are ordinary Catch2 model-level tests, tagged `[engine]`:

```
./cmake-build-debug/tests/magda_tests "[engine]"
```

Useful narrower tags while working on one part: `[plan]`, `[clip]`, `[exec]`, `[io]`,
`[transport]`, `[session]`, `[tap]`, `[offline]`, and inside those `[compiler]`, `[diff]`,
`[pdc]`, `[crossfade]`, `[voice]`, `[pool]`, `[stretch]`.

The parity harness has two of its own. `[nulldiff]` is everything the model-only target holds:
the comparators against known-bad pairs, the corpus's declarations, the native leg on its own,
and `[blocksize]` inside it for the invariance gate, which renders the whole corpus four times
and is the slowest thing in `magda_tests`. The two-engine runner is not here at all, since the
incumbent leg needs a `te::Edit`:

```
make test-juce JUCE_TEST="Null Diff Corpus"
```

Two properties the tests lean on and that are worth preserving:

**Plans and snapshots are canonical text.** `dumpPlan` and `dumpClipSnapshot` render them as
sorted, stable text, so a golden test is a diff of what the engine will play. A change that
alters either shows up as a readable diff rather than as a failing float comparison.

**Playback tests roll.** A test that skipped from one block to a distant one would be testing a
locate rather than playback, because a read that does not continue the last one is a seek. The
clip test rigs cue their readers where the transport is about to be, run the blocks in between,
and probe the one they care about.
