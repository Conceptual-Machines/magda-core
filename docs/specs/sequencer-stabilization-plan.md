# Sequencer Stabilization Plan

The Step Sequencer and Poly Sequencer currently have too much duplicated behavior
and too many state paths. Recent bugs around note entry, randomize repainting,
MIDI thru, and step-record lifecycle all came from the same design pressure.

## Tasks

1. Stabilize behavior with tests.
   - Cover randomize persistence and repaint notifications.
   - Cover MIDI thru while stopped and playing.
   - Cover step recording start, stop, final-step behavior, and restore/undo.
   - Cover mono and poly wherever the behavior is shared.

2. Extract shared header-control behavior.
   - `DeviceSlotComponent` should not reach directly into mono/poly plugin
     internals for randomize, MIDI thru, step record, record position, or max
     steps.
   - Use a small adapter that presents one sequencer-control surface to the
     header.

3. Normalize mono sequencer persistence.
   - Choose one message-thread source of truth for pattern data.
   - Prefer the poly approach: write through `ValueTree`, mirror into the audio
     step array in one controlled place, and make undo/redo part of the same
     path.

4. Extract shared MIDI sequencer runtime behavior.
   - MIDI thru, stopped-transport passthrough, record lifecycle, end-of-record
     behavior, and note-off safety should be shared or driven by common helpers.

5. Normalize UI update propagation.
   - Pattern changes should emit one clear UI invalidation signal.
   - UI should not need to know whether the change was a property update, child
     add/remove, randomize, AI apply, or step recording.

## Current First Cut

Start with tasks 1 and 2. They are low-risk and give us guardrails before
touching persistence or audio-thread playback internals.
