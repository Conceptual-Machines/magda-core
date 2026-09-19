# Native Arrangement audio recording (#2553)

The native engine already had `TakeRecorder`, asynchronous file writing, loop
passes, and audio peaks in `RecordTap`. This slice connects those pieces to the
same application-facing Record lifecycle used by native MIDI recording. An armed
track with a resolvable hardware audio input now creates an editable Arrangement
audio clip and a growing waveform preview.

## Ownership and lifecycle

`EngineHost` owns audio takes beside MIDI takes, outside the render plan. The
resolved callback-channel list, device generation, and reported latency form
the audio route identity. Record flushes
pending arm/input changes, creates the take, registers its stream with
`RecordThread`, and publishes it through the existing `RecordingFeed`. The live
epoch still decides whether the take may capture, so plan replacement cannot
feed a take under stale arm state.

Monitor state affects audibility only. An armed track records its selected
input with Monitor Off, and the take reads the hardware callback before the
track chain. Mono recordings remain one-channel files; stereo inputs preserve
their two packed callback channels. The device-reported input latency is handed
to `TakeRecorder`, which removes it from the head of the captured material.

Disarm, input rerouting, track deletion, transport stop, device stop, project
replacement, tempo/signature changes, and loop changes use the same close path.
The callback first receives a take set that no longer names the recorder, then
the record thread releases its stream, and only then is the file finalized.

The finished active file, placement, and loop-take model are installed through
`ClipManager::createRecordedAudioClip` before its sole notification. A listener
therefore cannot observe an audio clip whose alternatives have not arrived yet.
Existing overlap resolution remains the Arrangement publication policy.

## Live preview

The waveform overlay reads the audio take's existing `RecordTap`. Peaks are
computed from the exact samples offered to the recording queue, after input
latency correction, and the pass length comes from the callback transport. A
loop wrap replaces the displayed pass at the same accepted boundary that splits
the files. Stop or disarm removes the transient overlay as the completed clip
appears. Preview peaks are presentation data and never determine clip placement
or file content.

Each live pass retains up to 65,536 peak buckets at 1,024 samples per bucket
(about 23 minutes at 48 kHz). Longer recordings continue writing normally; only
the waveform preview stops gaining detail after that bound.

## Scope

This slice covers native Arrangement recording from the currently selected
hardware audio channel or stereo pair, including live previews, loop passes,
count-in/punch boundaries already supplied by the native transport, lifecycle
edits, and device-reported input-latency correction.

Session audio-slot recording remains separate. Opening and owning only the
selected hardware channels remains #2588; this implementation records from the
channels the application already opened and mapped. It does not implement or
change #2741 semantics. Manual latency calibration and recovery UI for files
from deleted/project-replaced tracks also remain outside this slice.

## Manual verification

1. Select a mono hardware input, arm an audio track with Monitor Off, press
   Record, play material, and stop. Verify the waveform grows while recording,
   the resulting file is mono, and playback comes from both speakers.
2. Repeat with a stereo pair. Verify left/right are not swapped and the live
   preview responds independently to each channel.
3. Start playback, punch Record in and out, then disarm during another take.
   Playback should continue after punch-out and each completed take should
   appear exactly once.
4. Record several loop passes with distinct material. Verify one alternative
   per pass, the last complete pass active, the partial final pass retained as
   an alternative, and the preview replacing its waveform at each wrap.
5. Record with a count-in and with a nonzero edit cursor. Count-in audio must
   not enter the file and the clip must begin at the requested beat.
6. Change the selected input or audio-device configuration while recording.
   The old take should close cleanly; subsequent capture should use the newly
   resolved callback channels.

## Automated evidence (2026-09-19)

- The Debug `magda_tests`, `magda_juce_tests`, and `magda_daw_app` targets
  built.
- The recorded-clip and audio-recorder filters passed 757 assertions across
  17 cases.
- `Engine Host Audio Input` passed eight callback cases, including device
  restart continuity and the guard that an unsupported Session audio target
  cannot become an Arrangement take.
- `Engine Host MIDI Recording` passed its 35 existing cases, `Engine Host
  Session Arrangement Capture` passed its ten cases, and `Magda Audio Engine
  Tests` passed its three cases.
- A Release build was not run.

The callback fixture covers Monitor Off capture, packed stereo content, the
live peak preview, device input-latency head correction, playback of the
materialized clip, disarm finalization, and device-stop cleanup. The recorder
unit suite remains the focused evidence for count-in and loop-pass file
boundaries. Hardware listening and latency feel still require the checks above and the broader
[recording smoke tests](../smoke-tests/recording.md).
