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
| Project, render, and capture lifecycle | Project transitions (#2833), opaque file handles (#2847), and asynchronous jobs (#2834, #2846) |

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
