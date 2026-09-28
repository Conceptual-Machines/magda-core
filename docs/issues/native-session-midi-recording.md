# Native Session MIDI recording (#2553)

## Plan

PR #2720 connected Arrangement MIDI recording. At that boundary, Session slot
recording still reported unwired methods from `MagdaAudioEngine`, although the
native recorder already supported slot-target capture. This slice connects that
application path.

1. Resolve a valid armed track and empty Session slot, preserving the existing
   distinction between an armed slot, a queued start, and active capture.
2. Drive capture from the native launch boundary, including quantized starts
   while playing and starts from stopped transport. Reuse `MidiTakeRecorder`,
   `RecordingFeed`, and `RecordTap` rather than introduce another capture path.
3. Publish the completed notes, controllers, and pitch bend atomically into the
   selected Session slot. Keep recording independent of monitor audibility and
   exclude piano-roll/drum-pad audition sources.
4. Reconcile stop, relaunch, disarm, routing changes, project replacement, and
   audio-device shutdown without losing or duplicating completed material.
   Arrangement and Session capture share the engine's one-take-per-track/material
   identity, so target transitions must close the old capture explicitly.
5. Verify the app-facing path with the simulated audio-device callback fixture,
   run Arrangement regressions and relevant native recorder/launcher tests, and
   build the application. Record the results and manual checks here.

## Scope

Session MIDI slot recording and its existing UI state/preview surface. Audio
recording, Session-to-Arrangement capture, count-in, hardware latency calibration,
and hardware-I/O ownership (#2588) remain separate work. #2553 stays open.

An empty slot has no clip on which to store launch quantization, and the model
has no separate global default. This slice uses the existing new-clip default
of one bar while transport rolls, and an immediate start while stopped. It does
not introduce a new preference or claim selectable quantization for empty slots.

## Capture lifecycle

The host publishes armed empty slots as native record targets. A recorder follows
the target's launch-handle incarnation, so its first sample is the launch sample
rather than a UI timer tick. Multiple armed targets share a launch gesture and
resolved boundary. The gesture is committed before stopped transport starts.
An inactive tap counts as cancellation only after a callback that started after
the committed gesture has finished processing. The callback-entry counter alone
cannot acknowledge a launch: an in-flight callback may not have read it yet.
Zero-frame callbacks do not acknowledge launch commands.

Arrangement recording intent is stored separately from active Session takes.
Ending a Session-only recording therefore cannot start Arrangement capture on a
later model update. Each track still owns at most one MIDI take. Changing targets
finishes the previous one, and re-clicking an active target finishes and disarms it.

Completed notes, CC, pitch bend, placement, loop length, and the Session slot index
are published in one `ClipManager` notification. If another edit has filled the
destination slot, the take is preserved in a free Arrangement region instead of
replacing either that slot or existing Arrangement material.

The preview reads the existing bounded `RecordTap`; it does not determine the
recorded timing. Input selection uses the same eligibility rules as Arrangement
MIDI recording, including Monitor Off capture and exclusion of audition sources.
The All MIDI Inputs source set is normalized before comparison so enumeration
order alone cannot retire a take.

Disarm and input changes finish the old Session capture. Transport and audio-device
stops finish captures; project replacement discards the outgoing project capture
as in the Arrangement path. Tempo, signature, loop, and device-configuration
changes finish Session takes rather than silently turning them into Arrangement
recordings. Existing Arrangement recording intent can resume after those changes.

## Manual verification pending

- Arm an empty MIDI slot while stopped, start recording, play notes, stop, and
  verify the resulting editable Session clip and its playback.
- Repeat during playback; input before the next bar boundary must not enter
  the clip. Multiple armed targets should begin on the same callback sample.
- Verify armed/queued/recording state follows actual capture, and monitor Off
  still records selected device input without admitting audition input.
- Exercise slot/track stop, relaunch, disarm, source changes, and project/device
  teardown, including a held note and controller/pitch-bend messages.
- Verify Arrangement MIDI recording and previews still work after a Session take.

Automated fixtures establish timing and lifecycle behavior. Application playing
and listening checks remain distinct from that evidence.

## Automated evidence

- Native engine and recorded-clip suite:
  `magda_tests '[engine]~[.],[clips][recording]'` passed 1,258 cases, with two
  existing expected tempo/warp failures and no unexpected failures. The first
  sandboxed run could not write test configuration and temporary audio files;
  the unrestricted rerun passed.
- `Engine Host MIDI Recording`: 18 cases / 128 assertions passed, covering the
  existing Arrangement cases plus Session capture from a stopped cursor,
  next-bar capture, shared starts, All MIDI Inputs/audition separation,
  re-click finalization, queued cancellation, and timer-only materialization.
- `Slot Launcher Tests`: 27 cases / 162 assertions passed, including handover
  from a recording-only Session state and empty-scene release.
- `Magda Audio Engine Tests`: 3 cases / 42 assertions passed.
- `Playback Position Timer Tests`: 5 cases / 34 assertions passed.
- Recorded-clip model suite: 4 cases / 27 assertions passed, including complete
  Session publication on the first notification and preservation of Arrangement
  material at the same beat range.
- Final Debug and Release app builds and the Debug JUCE test build passed.
- Pre-commit formatting, whitespace, conflict, and file-size checks passed.
