# Native Arrangement MIDI recording (#2553)

The native engine already had MIDI take capture, loop passes, and a record
writer, but the application's transport Record methods were unwired. A working
recorder test alone could not establish that pressing Record in MAGDA created
an editable clip. This slice connects that application boundary.

## Ownership and publication

`EngineHost` owns the recording lifecycle and the record thread. The transport
entry points delegate to that lifecycle; the Tracktion services instance does
not supply native recording state. Each eligible armed track gets a take owned
by `EngineSession`, outside the render plan. An unrelated plan rebuild must not
replace a running take.

The published session also carries the set of take keys allowed to capture.
That set can change without a graph change: with Monitor In, arming a track
leaves its input op in place. The host compares both topology and recording
eligibility when publishing. A changed graph is compiled first; an unchanged
graph with changed take permissions still receives a new session publication.
Record flushes pending model updates before installing takes, so the first
callback already has permission to capture them.

During an active recording, changed recorder routes are staged before publishing
the new monitoring route and session permissions. A newly armed take therefore
exists before any epoch can feed it; removed or changed sources are retired
before the new routing becomes visible. Capture setup must not be deferred until
after a new epoch has already started processing blocks.

Capture input is resolved independently of monitor routing. Monitor Off means
the input is inaudible, not that it disappears from the recording. Named inputs
select their device; All MIDI Inputs selects the registered hardware and virtual
device sources. Piano-roll and drum-pad audition sources are separate and must
never enter that selection.

Stopping capture has two synchronization boundaries: remove the take from the
audio callback's published set, then unregister its stream from `RecordThread`.
Only after both readers have released it may the message thread finish or
destroy it. Explicit stop and takes closed by model changes use the same
harvesting path. Device reconfiguration, project replacement, and shutdown
must also release streams before destroying their session.

The resulting clip is published through `ClipManager::createRecordedMidiClip`.
Placement, notes, CC, pitch bend, and alternative takes are populated before
listeners see the clip. Creating an empty clip, notifying, and silently filling
its notes afterwards leaves the engine snapshot and UI with stale content.
Recording placement comes from captured beats, not a UI timer or the cursor
position after Stop.

The live Arrangement preview reads the same take's `RecordTap`. The host gives
the tap bounded note storage when the take is created, then converts consistent
readings to the existing `RecordingPreview` model on the message thread. Held
notes and the pass length grow from captured blocks; released notes retain their
captured lengths. A loop wrap replaces the displayed pass, and stopping replaces
the transient overlay with the completed clip. Preview data is never inserted
into `ClipManager` or used to determine the recording's final timing.
The note preview currently holds up to 2,048 notes per pass, allocated before
capture starts; this display limit does not limit the recorded MIDI take.

## Scope

This slice covers Arrangement MIDI input recording: starting from the stopped
cursor, entering and leaving recording during playback, transport stop,
disarming, and loop takes. The existing recorder selects the last complete loop
pass and closes held notes at take boundaries.

Changing an input/source set, tempo, time signature, or loop region closes the
current take before starting another under the new settings. Unchanged settings
do not split takes. A seek ends recording. Device reconfiguration finalizes the
old capture before destroying its session. Deleting a track discards its pending
MIDI take with a diagnostic; replacing the project discards outgoing capture
instead of placing it on a reused track ID. This slice does not add a recovery
store for deleted-track MIDI recordings.

Audio recording, audio input delivery, audio recording overlays, Session slot
recording, Session-to-Arrangement capture, count-in, and hardware latency
calibration remain separate parts of #2553. Sample-accurate scheduled punch
boundaries are covered by the [native punch follow-up](native-punch-recording.md).
MIDI from the app's live queue currently arrives at
callback offset zero, so this does not claim sub-buffer input timestamps.

## Application verification

Run the normal `make run-console` workflow. No alternate checkout or launch
command is required.

1. In Arrangement, create an instrument track, select a MIDI device or the
   QWERTY input, arm it, and press Record from a nonzero cursor position. Play
   notes and stop. Open the resulting clip, verify its placement and note
   lengths, and play it back.
2. Repeat with Monitor Off. Input should be silent while recording, and the
   resulting clip should still contain the notes. Re-enable monitoring and
   verify playback.
3. Start ordinary playback, press Record, play, then press Record again.
   Playback should continue while the finished clip appears. Disarm during
   another take and verify the captured material appears once.
4. Record several loop passes with distinguishable notes. Stop partway into
   the next pass; check the active pass and the alternative takes. Hold a note
   across a wrap and another across Stop; neither should remain indefinitely
   open or appear twice.
5. With All MIDI Inputs selected, use a real/virtual input and click piano-roll
   preview notes. Only the device input should be recorded. With a named input,
   another device must not leak into the take.
6. Press Record with no eligible armed input. The transport UI must reconcile
   with the native host rather than remain stuck showing recording.
7. While recording, watch the growing Arrangement preview. Held notes should
   lengthen as they are captured, and stop lengthening when released. At a loop
   wrap, the preview should show the new pass. Stop or disarm and verify the
   overlay gives way to the completed clip without stale notes.

Automated tests establish routing, lifetime, placement, and model publication.
They do not establish the feel of live input, audible monitoring quality, or
physical latency. Those require the application checks above and the relevant
parts of [the recording smoke tests](../smoke-tests/recording.md). #2553 remains
open until its remaining recording surfaces are connected and verified.

## Automated evidence (2026-09-17)

- `magda_tests '[engine]~[.],[clips][recording]'`: 1,257 passing cases;
  two existing expected tempo/warp failures. This includes explicit source-list
  and atomic clip-publication regressions.
- `Engine Host MIDI Recording`: nine cases, 55 passing assertions. A simulated
  device is opened through `AudioDeviceManager` and drives the installed host
  callback. Coverage includes Monitor Off, arm followed immediately by Record
  with both changed and unchanged topology, audition exclusion, disarm,
  rolling punch-out, device stop, loop take selection/held notes, and project ID
  reuse. The final partial loop pass is retained, while the last complete pass
  supplies the active notes.
- `Playback Position Timer Tests`: five cases, 34 passing assertions, including
  rejected recording requests, direct transport-panel callbacks, punch-armed
  state, and the existing Session playhead ordering regression.
- `Magda Audio Engine Tests`: three cases, 42 passing assertions, including
  native recording-state queries and removal of the wired transport methods
  from the unwired-method diagnostic list.

The initial Debug and Release app builds passed. The user confirmed that MIDI
recording works in the application, then reported the missing live preview.
Preview verification is tracked separately; the automated results do not
substitute for playing and listening through MAGDA.

The preview follow-up passed Debug and Release builds, all eleven host callback
cases (80 assertions), and the engine API suite. Its two new cases verify first
callback visibility, held/released note lengths, equality with the completed
clip, loop-pass replacement, and clearing on punch-out, disarm, restart, and
project teardown. The Arrangement renderer also uses the actual pass duration
for note spacing before the first full beat. The user subsequently confirmed
that the live recording preview works in the application as well.
