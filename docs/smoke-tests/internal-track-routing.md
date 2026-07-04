# Smoke tests: internal track-to-track routing (#1690)

Manual verification checklist for track-as-input routing (audio + MIDI).
Run before merging changes that touch input routing, monitoring, or the
input selectors.

**Setup**: two audio tracks — **A** with a test tone (or any audio
device/clip), **B** empty. Audio device active.

## 1. Audio routing basics

- [ ] B's audio input selector lists other tracks after a separator
      (A present; B itself absent).
- [ ] Select A as B's input (`track:A`). B's meter stays silent while
      Monitor is Off and B is unarmed.
- [ ] Set B's Monitor to **In** → A's signal is audible/metering through B
      **without** arming.
- [ ] Set Monitor to **Auto** → silent until B is rec-armed, audible once
      armed.
- [ ] Set Monitor to **Off** → silent even when armed (input still records).

## 2. Monitor cycling stress (regression: meter-client UAF / deadlock)

- [ ] With A playing a tone and B routed to `track:A`, rapidly cycle B's
      Monitor Off→In→Auto→Off ~10 times, with the transport playing.
      No hang, no crash, audio keeps running.
- [ ] Same, while B is rec-armed. Toggling arm on/off repeatedly is also
      part of this check.
- [ ] Periodically (before releases): repeat this section under
      `MallocScribble=1 MallocPreScribble=1 MallocGuardEdges=1 make run-console`
      — any residual use-after-free crashes loudly instead of corrupting.

## 3. Recording

- [ ] Arm B, record in the arrangement → A's audio lands as a clip on B.
- [ ] Session view: record into a B clip slot with `track:A` input →
      slot clip contains A's audio.

## 4. MIDI routing

- [ ] Put a MIDI clip on A (with an instrument on B). B's MIDI input
      selector lists internal tracks after a separator.
- [ ] Select A as B's MIDI input → playing the arrangement drives B's
      instrument from A's MIDI.
- [ ] Arming/monitor changes on B do **not** silently replace the
      `track:A` MIDI input with "All MIDI Ins" (reopen the selector and
      check the selection survives).
- [ ] Record B in the arrangement → A's MIDI lands as a clip on B.

## 4b. MIDI To (source-side mirror)

- [ ] A's MIDI **output** selector lists other tracks after a separator.
- [ ] Pick B on A's out selector → B's MIDI input selector shows A
      (same edge, visible from both ends).
- [ ] With B listening, pick C on A's out selector → C listens, B is
      cleared (single destination).
- [ ] Pick a hardware MIDI out (or None) on A → A's track listeners are
      cleared.
- [ ] First selection works on a fresh project (selectors list tracks
      before any routing exists — regression for the populate bug).

## 4c. Groups

- [ ] Drag a track into a group → its output selector shows the group's
      name; ungroup → back to Master.

## 5. Mutual exclusivity + switching

- [ ] Setting a MIDI `track:` input on B clears its audio input, and vice
      versa (check both selectors after each change).
- [ ] Switch B's input A → hardware input → back to A: each switch takes
      effect cleanly (no doubled signal from target accumulation).

## 6. Guards

- [ ] B's own entry never appears in B's selectors (no self-routing).
- [ ] With B←A active, A's input selector does not offer B
      (cycle filtered). Same for a 3-track chain A→B→C: C's sources
      exclude A… actually A's selector excludes C — verify the *closing*
      edge is the one missing.

## 7. Persistence & deletion

- [ ] Save the project, reopen it → both the audio and MIDI `track:`
      routings are restored and functional (monitor In audible again).
- [ ] Delete source track A → B's input resets to None; no crash on the
      next play/rebuild; undo restores A (routing loss is acceptable,
      crash is not).

## 8. Surfaces parity

- [ ] The internal-track options and selection behave identically in all
      four places: Track Inspector, track headers, Mixer view, Session
      view IO strip.
