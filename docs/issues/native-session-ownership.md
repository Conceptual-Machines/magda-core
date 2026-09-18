# Native Session ownership (#2725)

Session ownership is per track. A launched clip or empty recording slot takes
that track from Arrangement; other tracks continue their Arrangement playback.
The transport button aggregates the same `TrackInfo::playbackMode` that drives
the track buttons and Arrangement dimming. Switching views does not change it.

## Queued and held state

Arming an empty slot does not take ownership. A quantized launch remains queued
until the native handle reaches its launch boundary. Arrangement continues until
that boundary, and the Session ownership indicators follow the acknowledged
handle state on the next UI update. A replacement queued on a track already
held by Session does not release that track while it waits.

Ordinary transport stop retains an existing clip's launch intent for restart,
as before. A track with that retained intent stays in Session mode while stopped;
an explicit return clears it. Recording targets are finalized by their recording
lifecycle and do not become retained clip intent unless a clip takes over.

Stopping a run and releasing a track are distinct engine operations. A track
return must release every handle that still holds it, including empty recording
slots and earlier slots in a handover. A quantized return keeps ownership until
its scheduled boundary; a global return cancels pending launches and releases
all tracks. Arrangement resumes at the current transport position.

Recording into an empty slot has no clip ID to report. Its launch handle still
defines ownership. Finishing recording can hand that handle to the newly
materialized Session clip without a gap in ownership. Cancellation and takes
that never started or were discarded must not leave a track held by an orphaned
recording target. A take that ran without MIDI input may still create a valid
empty clip.

## Scope

This change covers ownership reporting and release timing. Session performance
capture into Arrangement remains #2726: that requires its own capture lifecycle,
audio/MIDI clip materialization, and loop/seek policies. #2553 remains open.

## Automated verification

- Debug application, JUCE integration tests, and native test binary build.
- `Slot Launcher Tests`: 32 cases / 190 assertions pass.
- `Engine Host MIDI Recording`: 21 cases / 156 assertions pass, including
  actual Arrangement audio on unaffected and returned tracks.
- Native launch and section tests: 105 cases / 64,059 assertions pass. These
  cover timed audio/MIDI return boundaries and Arrangement recovery when a held
  recording handle is retired.
- Full native engine regression: 1,277 cases, with 1,275 passes and the two
  existing expected tempo/warp failures; no unexpected failures. This run needed
  unrestricted filesystem access for test configuration and temporary audio.
- `Magda Audio Engine Tests`: 3 cases / 42 assertions pass.
- `Playback Position Timer Tests`: 5 cases / 34 assertions pass.
- Pinned clang-format 17 and `git diff --check` pass; comment ratios inspected.

## Manual smoke pass (pending)

1. Put Arrangement material on three tracks. Start playback and record MIDI
   into an empty Session slot on the first track. Before its launch boundary,
   Arrangement continues; after it, the first track's button and the transport
   button turn orange and its Arrangement lane dims. The other tracks continue.
2. Launch a Session clip on the second track. Return only the first track to
   Arrangement. Its indicator and dimming clear at release; the transport
   button stays orange while the second track remains held.
3. Use global Back to Arrangement during Session playback and empty-slot
   recording. All indicators clear and Arrangement resumes at the current
   position. Repeat while recording is queued, before any notes are captured.
4. Finish a Session recording while playback continues, then launch another
   Session clip on that track. Check continuous ownership through both
   handovers, then return the track to Arrangement.
5. Switch between Session and Arrangement during each workflow. Playback and
   ownership remain unchanged. Repeat a quantized stop with both audio and MIDI
   Arrangement material to listen for an early return, a gap, or doubled sound.

Automated checks verify model state and rendered boundaries. This listening and
visual pass remains separate from those checks.
