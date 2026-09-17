# Values-time pruning of unused engine operations (#2627)

`resolvePlanValues` computes `OpValue::required` against the final published
render plan. A reverse dependency walk keeps operations whose outputs are read
by observable or stateful work. A silent consumer cuts dependencies only where
its executor actually stops reading those inputs; a Subtract operation reads
its dry input only while delta mode is enabled.

This is a values update, not a topology edit. Opening a gate or enabling delta
recomputes the mask before the next values publication. There is no dependency
walk, allocation, or graph rebuild in the audio callback. Serial and parallel
rendering use the same skip in `PlanExecutor::renderOp`, including the MIDI
prefix pass. Parallel scheduling still completes each skipped task and releases
its dependents.

## Conservative boundaries

Only Gain, SendTap, MixAudio, Subtract, and audio-only Fader operations can be
pruned. Every other kind is a root, even when it has no graph consumer:

- Clip/session sources keep their playback position and stretch state. Live
  inputs keep consuming their queues.
- Devices retain their existing processing and silence semantics. This change
  does not introduce plugin suspension or a new tail policy.
- Delay and Crossfade operations keep advancing their histories.
- Meters, modulation sources, MIDI merges/gates, MIDI-carrying faders, hardware
  inserts, and outputs keep their observable behavior. MIDI taps can be bound
  at runtime, so their presence cannot be inferred from topology alone.

Recording takes capture their inputs before plan execution; they do not read
the intermediate pure-operation outputs. If recording later gains a graph tap,
that tap must become a root before its upstream work may be pruned.

Never infer unused work from a zero static gain. Automation and modulation can
override that gain during processing. Existing track mute/solo expressed as a
zero gain is therefore not a new pruning opportunity in this implementation.
Likewise, keeping devices and sources active limits the CPU savings deliberately.
This is not a claim that muted tracks stop costing CPU.

Skipped outputs are cleared, including MIDI panic state. This still costs audio
buffer writes; the saving is the omitted mixing/copying/gain work, not complete
elimination of memory traffic. No numeric performance improvement is claimed
without a benchmark.

## Publication contract

The default required bit is true. A stale fingerprint, incomplete value table,
or invalid plan makes the resolver conservatively retain all work. Executor
fallback to unity values likewise retains all operations.

Resolve against the final plan after any operation insertion or index remap,
including crossfade insertion. Do not carry a mask from the previous topology.
Code constructing values by hand must rerun `resolveRequiredOps` after changing
`silent` or `subtractsDry`, or leave every operation required. The application
host resolves fresh values against its live plan on each values publication.

## Regression and listening checks

The required-operation tests cover silent gates, shared producers, dry-delta
dependencies, conservative roots and invalid-table fallback. Rendering checks
exercise mute/unmute and delta transitions in both executors. Existing engine
tests cover crossfades, delay histories, input monitoring, recording, modulation,
and automation around the same paths.

Run the focused regressions with `magda_tests '[required-ops]'`. The opt-in
`magda_tests '[required-ops-bench]'` benchmark compares pruned and all-required
execution of the same prepared graph: 64 retained sources feed a wide sum
behind a silent gate, alongside an audible input. It checks output equality
and source continuity before measuring; there is no timing assertion in CI.
This synthetic graph measures the avoided sum, not general project CPU usage.

For a live check, run `make run-console`, play an Arrangement loop and a Session
clip, and toggle rack-chain mute/solo and device/rack delta. Check meters and
monitoring as well as the first hit after re-enabling a path. Keep the prior
first-hit and loop-wrap fixes intact (see
[the first-hit investigation](first-hit-signalsmith-regression.md)): this work
does not change seek, priming, stretch processing, or transport logic.
Automated render parity is evidence
about the engine buffers, not proof of what a particular output device sounds
like. A live listening result must be recorded separately.

## Validation (2026-09-17)

`make debug` and `make release` pass. The focused regression selection passes
10 cases / 56 assertions. The hidden benchmark passes 1,094 assertions and,
with 30 samples in the local Debug build, measures mean block times of
7.67 microseconds with pruning and 15.35 microseconds with all operations
required. These are synthetic Debug measurements, not Release or live-project
performance figures. Live listening is pending.

The regular engine selection (`'[engine]~[.]'`, excluding opt-in stress tests)
passes 1,246 cases and reports two existing expected failures: the warp-aware
Session cycle and the tempo-map-derived seconds cache in
`test_clip_tempo_engine_sequences.cpp`. It reports 1,558,579 passing assertions
and four expected failing assertions, with exit status zero. Configuration
tests require access to their temporary cache directory; sandbox denial of
that write is not a persistence regression.
