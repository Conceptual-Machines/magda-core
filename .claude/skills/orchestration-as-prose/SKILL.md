---
name: orchestration-as-prose
description: The orchestration layer reads like prose. Load before writing or editing any function whose job is to coordinate other work — sync layers, publish paths, lifecycle setup, compile pipelines, anything named sync*/publish*/*Coordinator/*Synchronizer/*Bridge/*Manager. Covers the numbered-comment tell, one altitude per function, and where extraction stops paying.
---

# The Orchestration Layer Reads Like Prose

A function that coordinates says **what happens and in what order**. Nothing else. The mechanics
live one level down, behind names.

## The test, before the function ships

**Cover the comments. Does the body still tell you what happens?**

Yes -> done. No -> the steps want names, not comments. That is the whole rule.

## The tell

**Numbered step comments.** In this tree, `ClipSynchronizer::syncAudioClipToEngine` is 456 lines
carrying thirteen of them:

```cpp
// 1. Get Tracktion track
// 2. Check if clip already synced
// 3. CREATE new clip if doesn't exist
// 3b. REVERSE — must be handled before position/loop/offset sync.
...
// 13. CHANNELS — removed (L/R controls removed from Inspector)
```

Every number is a function nobody extracted. The `3b` and `5b` are the giveaway: steps were
inserted into a list that had outgrown being a list. Read it as the reference for what this skill
is against.

Smaller tells, same disease:

- A comment introducing a block. `// Set file offset (trim point in file)` is a function name with
  a `//` in front of it.
- Blank lines as paragraph breaks. If a body needs paragraphs, it needs functions.
- A local declared far from its use, or reused for two meanings. The dead `double bpm` at
  `ClipSynchronizer.cpp:916` survived precisely because nobody could see the whole body.
- Mixed altitude: a plan publish on one line, index arithmetic on the next.

## What it looks like when it works

`publishProject` (tests/EngineSessionScaffold.hpp) — compile the plan, resolve its values, collect
what the model holds, publish all four together:

```cpp
result.plan = std::make_shared<const RenderPlan>(compileRenderPlan(tracks, master));

PlanValues values;
resolvePlanValues(*result.plan, tracks, master, values, lanes);

const auto ids = collectRuntimeStateIds(tracks, master);
result.published = session.publish(result.plan, context, ids, std::move(values)).published;
```

No comment explains the order. The names carry it.

## Rules

- **One altitude per function.** Every line in a body answers the same size of question.
- **Name the step, delete the comment.** `// 6. UPDATE loop properties` becomes
  `updateLoopProperties(clip, ...)`. The comment was the name all along.
- **Ordering constraints go in the code, not a comment.** `// 6 ... (BEFORE offset —
  setLoopRangeBeats can reset offset)` is a rule the sequence should make unbreakable — assert it,
  or fold the two steps into one call that cannot be run out of order. A comment is not an
  enforcement mechanism.
- **A conditional in an orchestrator hides the story.** Give the predicate a name, so the body
  reads `if (clipNeedsProxy(clip))` rather than four clauses of state.
- **The orchestrator owns the order; the helpers own the how.** If you find yourself explaining
  *how* in the coordinating function, you are one level too deep.

## Where this does not apply

- **Audio-thread hot loops.** Extraction can cost, and correctness there is about what does not
  happen (see the `audio-thread` skill). The declarative-loop epic (#2143) carries a SIMD-first
  exception list for the same reason.
- **Leaf mathematics.** A DSP kernel is not orchestration; it is the thing being orchestrated.
- **Generated or mechanical surfaces.** A forwarding wall (`MagdaAudioEngine`) is not prose and
  should not pretend to be.

## Where extraction stops paying

A step pulled into a member function with nine parameters has moved the mess, not removed it. When
that happens the decomposition is wrong, not the function count. Reach instead for:

- a small struct carrying the state the steps share, with the steps as its methods
- a lambda in the body, when the step is genuinely used once and closes over local state
- a different seam: if the parameters cluster, that cluster is the object you were missing

## With the other skills

`comments-not-essays` says a comment is a line or two. This says where the pressure to write a long
one comes from: a body that does not read. Fix the body and the comment stops being needed.
