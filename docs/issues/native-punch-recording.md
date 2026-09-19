# Native scheduled punch recording (#2553)

The punch markers already reached the timeline controller, but the native
engine's punch callbacks were unwired. Recording started and stopped when a
message-thread playhead update noticed a marker. A callback could therefore
record past punch-out or miss input immediately after punch-in.

Scheduled punch belongs to the native recording path. Playback and input
monitoring continue outside the recording window. The native engine must
receive recording intent before punch-in, and the audio callback determines
which samples enter a take. The message thread publishes completed clips
afterward; its polling interval must not determine their boundaries.

The transport splits callbacks at enabled punch boundaries. A boundary between
device samples uses the first sample at or after the marker. Session capture
receives recording-window events in the same ordered queue as slot playback
events, so a run that starts and ends between message-thread updates still has
both recording boundaries. Recording generations distinguish a new request
from delayed completion of an earlier one.

Moving punch-in before capture starts reschedules pending Session slots. Once
a take has started, moving punch-in forward does not interrupt it. Punch-out
also cancels slots waiting for a later launch boundary. Independently started
Session slot recording remains independent of Arrangement punch markers.

## Automated coverage

The transport and Session capture tests check sample rounding, ordered window
events, and stale generations. Host MIDI regressions exercise callbacks that
cross both markers, held notes, live marker edits, delayed message dispatch,
stop/restart, Session slot scheduling, and punch-out coinciding with loop or
callback end. Timeline and playback timer tests cover early native recording
intent, cancellation, rejected requests, and the legacy engine fallback.

## Application checks

Use the native engine with an armed instrument track and a selected MIDI input.

1. Place punch-in and punch-out ahead of the edit cursor. Press Record and
   play before, through, and after the window. Playback must continue after
   punch-out. The resulting clip must cover the recording window, with no
   input captured outside it.
2. Hold a note from inside the window through punch-out. The recorded note
   must end at punch-out, while live monitoring remains available.
3. Start playback first, then press Record before punch-in. Cancel Record
   before reaching the marker. Playback must continue and no take may appear
   later when the marker is crossed.
4. Repeat with only punch-in enabled, only punch-out enabled, and both
   disabled. Ordinary manual Record and Stop must retain their behavior.
5. Move the markers and change their enable controls during playback. Future
   boundaries must follow the new settings without a seek or playback stop.
6. Enable a transport loop that crosses punch-out, then record. Check that
   wrapping inside an audio callback does not lose the punch-out event or
   create a second take unintentionally.
7. Repeat with a Session performance captured into Arrangement. Material
   played before punch-in or after punch-out must not enter the Arrangement
   capture, including short Session runs completed between UI updates.
8. Arm an empty Session MIDI slot and enable punch-in. Verify that the slot
   starts recording at its intended scheduled boundary, not when Record is
   pressed. Repeat with count-in enabled and while already playing.

Automated checks must establish callback boundaries, held-note closure,
cancellation, live setting changes, loop wrapping, and delayed message-thread
harvesting independently of the rendered UI. These application checks still
require hands-on verification.

Hardware audio-I/O ownership (#2588), physical latency calibration, and the
remaining native audio-input recording work are separate. This slice does
not complete all of #2553.
