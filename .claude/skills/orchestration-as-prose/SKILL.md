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

**Numbered step comments.** Every number is a function nobody extracted. `ClipSynchronizer::syncAudioClipToEngine` carried thirteen of them across 456 lines, and the `3b` and `5b` were the giveaway — steps being inserted into a list that had outgrown being a list.

Smaller tells, same disease:

- A comment introducing a block. `// Set file offset (trim point in file)` is a function name with a `//` in front of it.
- Braces labelled on the way out: `}  // if (teClip)`, `}  // else (already synced)`. Structure you cannot see without being told.
- Blank lines as paragraph breaks. If a body needs paragraphs, it needs functions.
- A local declared far from its use, or reused for two meanings. Four dead `double bpm` locals lived for years in that file because nobody could see a whole body.
- Mixed altitude: a plan publish on one line, index arithmetic on the next.

## What it looks like when it works

Both of those functions have been through this. The tail of `syncAudioClipToEngine`
(`magda/daw/audio/session/ClipSynchronizer.cpp`), which is the shape to copy:

```cpp
    syncPlacement(*teClip, *clip);
    needsGraphReallocation |= syncStretchMode(*teClip, *clip, sourceBeats);

    // A reversed clip's tempo mode, loop range and offset belong to Tracktion
    // for as long as it stays reversed.
    if (!reversed)
        syncTempoMode(*teClip, *clip, sourceBeats);

    syncWarpMarkers(clipId, *teClip, *clip);

    if (!reversed)
        syncLoopRangeAndOffset(edit_, *teClip, *clip, sourceBeats);

    syncPitch(*teClip, *clip);
    syncBeatDetection(*teClip, *clip);
    syncMix(*teClip, *clip);
    syncFades(*teClip, *clip);
```

The order is still exactly what Tracktion demands. What changed is that you can read it, and
the two comments left are the two things the names genuinely cannot say — why a reversed clip
skips three of the steps.

| | before | after |
|---|---|---|
| `syncAudioClipToEngine` | 456 lines, 13 numbered steps | 57 |
| `syncClipPropertyToEngine` | 241 lines, 7 levels of nesting | 13 |

Behaviour is identical, and the null-diff corpus says so rather than the author: all 74 cases
produce byte-identical peak, rms and shift figures either side of the change. That is the other
half of this skill — an orchestration refactor is safe exactly when something end-to-end can
tell you nothing moved.

`publishProject` (tests/EngineSessionScaffold.hpp) is the same shape written that way to begin
with: compile the plan, resolve its values, collect what the model holds, publish the four
together. No comment explains the order because the names carry it.

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
