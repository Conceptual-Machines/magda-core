# Smoke tests: recording (#1895)

Manual verification for live input, monitoring and recording. Run before
merging changes that touch the input path, the take recorders or the session
capture, and as the hands-on pass #1895 budgets for the cutover.

This checklist covers only what needs a human and an interface. Everything
that can be asserted offline already is, in `test_take_recorder`,
`test_midi_take_recorder`, `test_session_capture` and `test_take_across_swap`;
see the re-scoping argument on #2466 for why the two-engine corpus does not
extend here.

**Setup**: an audio interface with a physical input, headphones, one audio
track armed, one MIDI track with an instrument. Where a check says *both
engines*, run it once per engine while #1897 ships dual-engine.

## What the engine does

The conventions the native tests encode. They are what "correct" means below —
a test asserts the convention it was written from and would pass just as
happily with the sign flipped, so these are the lines that need a human to
confirm once.

| Convention | Rule |
|---|---|
| Input latency | What arrives has already happened. A **positive** latency drops that many samples from the head of the take; a **negative** one pads the head with that much silence. |
| Count-in | Never part of the take. The take starts where the count-in ends. |
| Loop record | One file per pass, loop-aligned. The active take is the last **whole** pass. |
| Lead-in | A first pass that did not start on the loop is a lead-in, not a take — unless there is no whole pass, and then it stays where it was recorded. |
| Slot take | Starts on the sample its launch fired on, ends where the run ended. A re-launch closes the take rather than extending it. |
| Held MIDI note | A note still down when the take ends lasts until it does. Across a loop wrap it belongs to one pass, once. |

## 1. Input latency is the right number

- [ ] Loopback: feed an output back into an input physically. Record a sharp
      transient played from the arrangement at a known beat.
- [ ] The recorded transient lands on that beat, not before or after it. This
      is the one check that says the sign and the magnitude are both right —
      an offline test cannot, because it is asserting the sign it was written
      from.
- [ ] Both engines put it on the same sample.
- [ ] Change the buffer size and repeat. The transient stays on the beat.
- [ ] Set a manual offset adjustment in settings, both positive and negative.
      The take moves the way the table above says.

## 2. Monitoring

- [ ] Monitor **In** on an armed track: input is audible, unarmed and armed
      alike.
- [ ] Monitor **Auto**: silent until armed, audible once armed.
- [ ] Monitor **Off**: silent even when armed, and the take still records.
- [ ] Round trip is playable — sing or play into it and it does not feel late.
      Compare against the interface's own direct monitoring if it has one.
- [ ] Both engines: the monitored signal is at the same level, and neither is
      audibly later than the other.
- [ ] Monitored input is not doubled when the track also plays back.

## 3. Arming and punching

- [ ] Arm and disarm while the transport rolls. No click, no dropout.
- [ ] Punch in and out mid-pass while playing. The take covers what was
      punched and nothing else.
- [ ] Disarm mid-pass: the take closes and what was recorded appears as a
      clip. Nothing is silently lost.
- [ ] Stop mid-pass: same.

## 4. Loop recording

- [ ] Record several loop passes. Each pass is its own take, aligned to the
      loop.
- [ ] The take shown after stopping is the last whole pass.
- [ ] Cycle through the alternatives — each one holds the pass it was played
      on, in the right order.
- [ ] Start recording partway through the first loop: that lead-in is not
      offered as a take when a whole pass follows it.
- [ ] Record one partial pass only: it survives, where it was recorded.

## 5. MIDI

- [ ] Play across a tempo change. Notes land where they were played, not
      where the new tempo would put them.
- [ ] Hold a note across the loop wrap. It appears once, in one pass.
- [ ] Hold a note when recording stops. It ends where the take does, not
      earlier and not open.
- [ ] CC and pitch bend survive with their positions.

## 6. Session slots

- [ ] Arm an empty slot and launch it. Recording starts on the beat the
      launch fired, at each quantization setting — including None.
- [ ] Re-launch mid-run: the take closes and a second one begins. The first
      is not extended.
- [ ] A scene launch across several armed tracks: every take starts on the
      same sample.
- [ ] Capture the session to the arrangement. Clips land on the beats their
      runs were launched on.
- [ ] Let the transport loop underneath a run. The captured clip is placed
      once.

## 7. Editing while recording (#2465)

The take has to survive a plan swap, and any edit is a swap.

- [ ] Record a long pass. While it rolls, add a device to **another** track.
      The resulting file is one unbroken take — no gap, no repeat, no click at
      the swap.
- [ ] Same, but edit the armed track's own chain.
- [ ] Rename something, move a clip, add a track. Same result each time.
- [ ] Delete the recording track mid-pass. Nothing already written is lost and
      no file is left open.
