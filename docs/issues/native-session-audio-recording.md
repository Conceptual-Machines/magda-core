# Native Session audio-slot recording (#2553)

An empty Session slot on an armed audio track now records the selected hardware
input under the native engine. The take follows the slot's quantized run rather
than the Arrangement transport, publishes a waveform in that slot while it is
open, and becomes a looping beat-mode audio clip when the run ends.

## Material and ownership

A selected audio input makes the slot an audio target. This takes precedence
over a MIDI route, including the default `all`, because one Session slot can
hold only one clip. With no audio input, the existing MIDI path is unchanged.

The host creates an audio `TakeRecorder` against the empty slot's
`SlotRunTarget`. The launch handle supplies the exact start and end samples, so
the take shares MIDI's next-bar launch, count-in and punch scheduling. The
recording handle owns the track until the completed clip is published into the
same slot; the new clip then continues that run without returning briefly to
Arrangement.

Re-click, per-track or global Back to Arrangement, disarm, input changes,
device stop and project teardown use the existing Session target lifecycle.
Input latency is removed from the recorded head just as it is for Arrangement
audio.

## Publication

`ClipManager::createRecordedAudioClip` accepts Arrangement or Session
placement. A Session recording is installed with its source, scene index,
musical loop length and beat-mode interpretation before the single model
notification. Its placement begins at zero inside the slot; the file itself is
unchanged.

If another edit fills the destination while recording, that clip keeps the
slot and the finished audio is preserved in a free Arrangement region. An empty
or discarded take releases the recording handle instead of leaving the track
owned by Session.

## Manual verification

1. Select a mono input, arm the track, click an empty Session slot and start
   recording. Verify the waveform grows in that slot and no Arrangement clip
   appears.
2. Stop recording. Verify one audio clip occupies the chosen slot, immediately
   plays its recorded audio, and loops on its recorded musical length.
3. Repeat with a stereo pair. Verify channel order and the live waveform.
4. While rolling, arm a slot and verify capture begins on the next bar. Repeat
   with count-in and with punch markers.
5. Re-click the recording slot, use per-track Back to Arrangement, and use
   global Back to Arrangement. Each should finish the file once and release
   ownership at the requested boundary.
6. Leave MIDI input on `all` while an audio input is selected. The slot must
   contain audio, not an empty MIDI clip.

## Automated evidence

- The audio callback fixture records mono hardware input into a Session slot,
  checks its waveform target, file length, beat-mode loop and ownership
  handover, with a live MIDI source present.
- The model test publishes a recorded audio source directly into a selected
  slot without disturbing Arrangement material.
- `TakeRecorder` already covers launch-sample start, run-sample end, paused
  stops, transport loops, latency correction and multi-track scene boundaries.
