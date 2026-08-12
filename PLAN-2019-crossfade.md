# PLAN #2019: crossfade changed edges across a plan swap

This is a prescriptive spec. Every decision is made. Implement it as written; do
not redesign, do not add options, do not ask which way. Local file, stays
untracked.

## What to build

A post-compiler pass that inserts Crossfade ops into a newly compiled plan on
the audio edges the edit moved, an equal-gain ramp in the executor that renders
them, and a retirement protocol that removes them at the next edit. No other
mechanism.

## Hard constraints (violating any of these is a bug)

1. Any op bound to a store-owned instance appears exactly once in the published
   plan. No shadow subgraphs, no rendering old and new plans side by side.
2. The compiler stays a pure function of the model. Fade insertion is a pass
   over its output, never a compiler mode or flag.
3. Both executors render the fade through one op body and stay bit-identical at
   every thread count.
4. MIDI edges are never faded.
5. One swap per edit. Retirement must not compile or publish anything by
   itself.

## Components, in build order

### 1. Plan vocabulary (RenderPlan.hpp/.cpp)

- `OpKind::Crossfade`, arity exactly 2: input 0 = the edge as it was, input 1 =
  the edge as it is. One audio output.
- `OpRole::EdgeCrossfade`. The fade is keyed to the op it feeds and the slot it
  fills. The consumer's role is encoded in `OpKey::index`:
  `constexpr int kCrossfadeRoleStride = 1024;`
  `crossfadeIndex(consumerRole, slot) = int(consumerRole) * stride + slot`,
  with `crossfadeConsumerRole()` and `crossfadeSlot()` decoders. This is what
  keeps two fades at one model location (fader vs meter) from colliding in the
  differ's hash join.
- validatePlan MUST check: role EdgeCrossfade iff kind Crossfade; both inputs
  valid; exactly one audio output; index decodes to a non-negative slot.
- toString prints the decoded consumer role and slot, not the raw index.

### 2. The pass (plan/PlanCrossfade.hpp/.cpp)

API:

```cpp
struct CrossfadedPlan { RenderPlan plan; int inserted = 0; int unfaded = 0; };
CrossfadedPlan insertCrossfades(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                                const std::vector<char>& stillFading = {});
```

Decision rule. For every op in the new plan that the differ did NOT carry, that
existed in the old plan under the same key with the same kind and the same
input count: for each audio input slot whose producer moved, insert a fade IF
AND ONLY IF all of the following hold. Otherwise increment `unfaded` and leave
the edge alone.

- The old producer's key still exists in the new plan (rules out removal).
- Its new index is strictly less than the consumer's (rules out reorder cycles;
  ops are in dependency order, so this is one integer comparison and the pass
  needs no cycle detection).
- The old producer is computing what it used to: carried by the differ AND all
  of its transitive inputs likewise, computed in one forward pass over the new
  plan into an `unchanged_` bitmap. Seed rule below.
- The consumer is not a Delay op (a delay's sample count is resolved from the
  fan-in it feeds; never interpose there; the fade lands on the delay's own
  input via that consumer instead).
- Not (old side Live AND consumer Deterministic): the liveness provenance rule
  validatePlan enforces must survive the pass.
- slot < kCrossfadeRoleStride.

Ops whose input COUNT changed (a sum gaining or losing an edge): count one
`unfaded` if any input is audio, insert nothing. Ops the edit created are
skipped entirely; whatever they displaced fades one op downstream.

Running fades STACK; spent fades collapse. (Revised 2026-08-06: the original
spec mandated collapse always; that steps by the unfinished fraction of the
ramp, which one millisecond into a five-millisecond fade is most of the click
this slice exists to remove.) When a second edit lands on an edge whose fade is
still running, the running chain is re-emitted as it is (same keys, same sides,
innermost first, so every ramp is adopted) and the new fade stacks in front,
reading the outermost re-emitted fade as its old side, at depth + 1. The first
sample after the swap is then the exact next sample of the ramp the previous
epoch was rendering. When the old fade is SPENT, it collapses: its output is
identically its destination, so fading from the destination is exact. Depth is
capped (3 bits, max 8); at the cap, degrade to collapse of the running chain,
which is the bounded residual step, never to nothing. Chains are self-limiting
in practice: a chain only deepens while edits land within one fade length, and
the whole chain retires together because the outermost spending implies every
inner one has (they started earlier at the same length).

Retirement. `stillFading` is `PlanExecutor::unfinishedCrossfades()` from the
epoch rendering oldPlan, indexed by oldPlan OpId. An edge that arrived where
its chain was heading: if the outermost fade is still running, re-emit the
whole chain on its existing keys so every ramp carries; if it is not running,
emit nothing. This is the whole retirement mechanism: a spent chain disappears
at the next publish and the plan returns to byte-for-byte compiler output.

unchanged_ seed rule. Before the forward pass, mark the consumer of every
spent, arrived fade as carried-equivalent (the arrived-and-not-running
predicate above). A spent fade is a pass-through of the port its consumer now
reads directly, so the consumer IS computing what it computed; without this
seed, every retirement marks its whole downstream (master mix included) as
changed and an unrelated simultaneous edit loses its fade.

Rebuild. Inserting mid-plan MUST preserve dependency order: rebuild the op
vector with a moved[] remap, insert each fade immediately before its consumer,
remap outputOps, then `bakeScheduling(built)`. Start the rebuild from a full
copy of the plan struct and replace ops/outputOps, so a field added to
RenderPlan later is carried, not silently dropped. Fades on the same consumer
must arrive in slot order.

Call order (values are refused against any other plan):

```cpp
auto plan  = compileRenderPlan(tracks, master);
auto faded = insertCrossfades(*livePlan, plan, liveExecutor.unfinishedCrossfades());
resolvePlanValues(faded.plan, tracks, master, values);
session.publish(std::make_shared<const RenderPlan>(std::move(faded.plan)), ...);
```

### 3. The ramp (exec/PlanExecutor.hpp/.cpp)

- `kCrossfadeSeconds = 0.005`. Equal-gain LINEAR ramp, not constant-power: the
  two sides are the same material a moment apart, correlated signals sum to
  full amplitude, a power law bulges.
- `class CrossfadeRamp`: `prepare(lengthSamples)`, `hasConfiguration(length)`,
  `spent()`, `process(dest, oldSide, newSide, numSamples)`. Position is counted
  off `remaining_`, never off block counts, so the fade is the same length at
  every block size. `remaining_` is `std::atomic<int>`, relaxed both ways (the
  publishing thread polls `spent()` while the audio thread writes). Past the
  end of the ramp, copy the new side through (guard the copy when dest aliases
  it), so a straddling block is not a caller special case.
- Executor prepare: one ramp per Crossfade op. Adopt the ramp from `previous`
  when the differ carried the op AND `hasConfiguration` matches (a resumed fade
  resumes; a rate change restarts). Held by shared_ptr exactly like delay
  lines.
- Enable gate: the ramp runs only if the resolved port latency of input 0
  equals input 1. Unequal means the edit moved latency along the edge; a delay
  line built now starts flushed, so compensating the old side would fade in
  from silence. The op stays in the plan and passes input 1 through.
- Render body (shared by both executors via renderOp): no value entry (a fade
  has no model location; resolvePlanValues skips it like Delay). Runs on
  silent blocks so a fade finishes during a mute instead of resuming after it.
  If no ramp or spent: pass input 1 through (elided when in-place). Otherwise
  `ramp->process(out, in0, in1, n)`.
- `unfinishedCrossfades()`: vector<char> sized to the prepared plan, 1 where a
  ramp exists and is not spent. Plus `activeCrossfades()` and
  `carriedCrossfades()` counters for tests.

### 4. Layout (exec/PlanLayout.cpp)

- In-place: a Crossfade may write over input 1 (every sample is read before the
  same index is written). Never input 0.
- Latency: a Crossfade's produced latency is `arriving(inputs[1])`. THE NEW
  SIDE, NOT THE MAX. A disabled fade outputs only the new side; a running fade
  requires the sides equal; a spent fade is a pass-through of the new side. Max
  is wrong whenever the old side is more latent and misaligns every parallel
  path downstream until retirement. This is a named case in
  resolvePlanLatency beside the Delay transparency case.

### 5. Values (exec/PlanValues.cpp)

Crossfade resolves to nothing, exactly like Delay: early return on kind,
EdgeCrossfade listed in the role switch. Unity is what the executor reads.

## Explicitly out of scope

- Fading removals (re-emitting the removed device op is a possible follow-up,
  not this slice).
- Fading MIDI, fading value changes (bypass, mute, levels: the value layer's
  business), launcher fades (#1894).
- Any smoothing of edges the decision rule refuses. They step, as they do
  today, and `unfaded` counts them.

## Tests that MUST exist (tests/test_plan_crossfade.cpp)

1. Unchanged model republished: 0 inserted, 0 unfaded, fingerprint identical.
2. Insert a device: exactly 1 fade, keyed to the consumer role and slot, old
   side = displaced producer, new side = new device, consumer rewired to the
   fade. validatePlan clean.
3. Remove a device: 0 inserted, 1 unfaded.
4. Reorder two devices: exactly 1 inserted (the surviving edge), 2 unfaded.
5. Arity change (send added): 0 inserted, unfaded > 0.
6. Second edit mid-fade: new fade reads the first fade's target as old side,
   never the fade op itself; exactly 1 fade in the result.
7. Faded plan invariants: validatePlan empty, carriesSchedule true, outputOps
   still name Output ops, op count = compiled + inserted.
8. Ramp shape: first block starts at the old signal, strictly monotonic, ramp
   is exactly its stated length (still fading one block short, silent/settled
   one block past).
9. Resume: republish the same model mid-fade; carriedCrossfades == 1; the ramp
   continues from where it was, no restart.
10. Retirement: publish an edit, render past the ramp, republish the same
    model; 0 inserted, activeCrossfades == 0, fingerprint equals a fresh
    compile.
11. Latency gate, BOTH directions: latent device inserted after the edge
    (fade off, passes new side) AND old side more latent than new (fade off,
    downstream compensation reads the new side's latency; assert path
    alignment at the fan-in).
12. Parallel identity: a faded plan renders bit-identical on the reference
    executor and the parallel executor at several thread counts, across the
    whole ramp and past its end.

## Definition of done

All tests above green in the Catch2 suite, magda_juce_tests builds, TSAN clean
(`make test-tsan TEST="[crossfade]"`), and a publish with no structural change
still produces a byte-identical plan (fingerprint check in test 1 and test 10).
