# Remote API batch execution decision

Decision for #2845 (2026-09-26): do not add `batch.execute` to the Remote API.

The audit in #2817 found no remaining workflow that needs arbitrary composition
of existing writes. The workflows requiring one mutation and one undo step have
purpose-built contracts:

| Workflow | Atomic boundary |
| --- | --- |
| Chord progression replacement and extraction | `chordTrack.replaceProgression` (#2831) and the addressed detect/extract/send contract (#2843) |
| Scene and slot editing | Session scene commands (#2842), explicit clip placement (#2844), and slot recording (#2841) |
| Device, chain, routing, and sidechain changes | Device replacement/preset and chain preset commands (#2814, #2836, #2839), routing (#2832), sends (#2837), and sidechains (#2838) |
| Project, render, and capture lifecycle | Project transitions (#2833), absolute file paths (#2886), and asynchronous jobs (#2834, #2846) |

The still-open slices above have defined destinations and failure policies. A
generic batch would not make them atomic: project loading, recording, rendering,
and device instantiation can produce engine or filesystem effects that the
ordinary undo stack cannot roll back. Allowing those operations in a batch
would promise more than the implementation can guarantee. Excluding them leaves
only content edits for which callers have no demonstrated need for an
arbitrary multi-operation transaction.

If a concrete workflow later cannot be served by a purpose-built operation,
reopen this decision with its input, desired result, and rollback boundary. A
batch proposal must then define a fixed edit-only allowlist; validate every
child schema and permission before mutation; use one outer `expectedRevision`
and `requestId`; bound count, payload, and cost; reject nesting; return ordered
results; and prove full rollback, one undo step, one revision, and correct
change-topic publication. It must exclude lifecycle, transport, session
performance, hardware, and asynchronous jobs. No public begin/end transaction
calls should be exposed.

## Reopened: `batch.apply` (#3377, 2026-11-21)

**Workflow.** An agent building a project's content over MCP: tracks, a MIDI
clip with notes on each, mixer levels and a loop. The content varies freely, so
a purpose-built "build a project" operation would have to accept everything the
individual writes already do. Before this it took about 35 calls and as many
undo steps, and a failure halfway left a half-built project.

**Rollback boundary.** Model edits only. Devices are added with their own calls
first, then one batch carries everything else.

**Allowlist.** `tracks.create`, `tracks.update`, `clips.createMidi`,
`clips.addMidiEvents`, `clips.replaceMidiEvents`, `clips.updateMidiEvents`,
`clips.deleteMidiEvents`, `clips.update`, `clips.duplicate`, `clips.transpose`,
`clips.move`, `clips.resize`, `sends.create`, `sends.update`,
`project.setLoopRange`. Each records its changes as undo commands, which is what
lets a failed batch roll back.

Left out although #3377 proposed them: `project.setTempo`,
`project.setTimeSignature`, `devices.setParameter` and `devices.setParameters`.
None of them records an undo command today, so a batch could not take them back.
They can join once they do.

**Contract.**

- Every step's method, permission and schema is checked first, as if sent
  alone, and nothing runs if one fails. The error names the step.
- A step may use an earlier step's field as `{"$ref": "N.id"}`. Validation
  checks that `N` is earlier and that its output declares the field; the value
  itself is resolved while applying.
- At most 256 steps and 16,384 MIDI events in total, inside the transport's
  request size limit. No nesting.
- One outer `expectedRevision` and `requestId`; steps carry neither.
- The reply is `{"revision": N, "results": [...]}`, one entry per step in
  order, each with the step's `id` or `ids` as in the #3376 summary form.
- One undo step and one revision. Each change topic the steps touch is
  published once, after commit. A failure while applying undoes every command
  the batch recorded, with no revision and no publication.
- No public begin/end transaction calls.
