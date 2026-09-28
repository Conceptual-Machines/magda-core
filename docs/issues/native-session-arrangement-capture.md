# Session performance capture into Arrangement (#2726)

Arrangement recording captures existing Session audio and MIDI clips from the
native launcher's run events. Tracks do not need hardware-input arming to capture
their Session performance. Recording into an empty Session slot remains a
separate workflow; playing Session material without Arrangement recording creates
no Arrangement clips.

## Timing and source identity

The audio callback stamps launch, replacement and stop boundaries. Collection on
the message thread materializes those spans without moving them to the collection
time. A scene therefore preserves its common launch position even if collection
is delayed.

Recording started during an existing run captures from the record boundary, with
the source phase heard there. Punching out ends capture while Session playback
continues. A subsequent punch-in captures only the new interval. Each material
revision has an immutable source snapshot so editing or replacing a slot does not
change earlier captured content. A plan swap alone does not start another run.

Captured audio keeps its original event geometry and an envelope window relative
to the new placement. Cropping the recorded interval therefore preserves source
phase, reverse/warp mapping, gain fades and speed ramps. The window is saved with
the clip, so reopening the project preserves the same playback.

## Placement policies

- Captured spans replace overlapping Arrangement material only within their
  recorded range. They keep the actual launch/capture position rather than
  shifting into a free region.
- A transport loop does not restart a Session run. The existing native capture
  convention measures its uninterrupted duration on the monotonic beat axis and
  places it once at its timeline start. Later Session launches use their own
  actual timeline positions; overlap replacement applies to those spans. A
  Session clip's own loop retriggers are separate runs, each captured at its
  actual boundary.
- Seeking punches recording out before the locate, matching native input
  recording. Recording must be started again at the destination.
- Individual/global Back to Arrangement close the corresponding spans at the
  native release boundaries. Captured clips remain silent while Session owns
  their track, then Arrangement resumes at the current position.
- Transport and device shutdown finish capture. Project replacement discards
  outgoing capture so reused track IDs cannot receive material from the prior
  project.

## Automated validation

- Debug app, native tests and JUCE tests build successfully.
- Broad native/model run: 1,296 cases passed, with the two pre-existing
  `[!mayfail]` tempo/warp cases reported as expected failures. The command exits
  successfully (0); no new tests are marked as allowed failures.
- Final capture, voice, compiler and resize checks: 92 cases, 2,094 assertions.
- Host Session capture: 9 cases, 53 assertions, including mixed audio/MIDI scenes.
- Existing host MIDI recording: 21 cases, 156 assertions. Slot Launcher,
  Magda Audio Engine and Playback Position Timer suites also pass.
- Pinned clang-format 17, whitespace, file-size and comment-ratio checks completed.

## Manual smoke pass (pending)

1. Put Arrangement material on several tracks and existing audio/MIDI clips in
   Session slots. Leave the Session tracks unarmed for input, start Arrangement
   recording, and launch one clip, then a scene.
2. Confirm unaffected tracks keep playing Arrangement. Both Back to Arrangement
   indicator sizes and Arrangement dimming follow the Session-owned tracks.
3. Stop recording while playback continues. Check captured positions, source
   offsets, loop content and lengths. Return the tracks to Arrangement and
   compare the captured performance by listening.
4. Punch recording in and out during a long Session run. Verify each interval
   starts at the heard source phase and does not recapture earlier material.
5. Replace a Session clip while capturing, change an unrelated track's device
   chain, switch views, and delay collection with a busy UI. Check source
   identity, placement and scene alignment remain correct.
6. Exercise individual/global returns, a transport loop, stop, seek and project
   replacement. Verify no duplicate spans or material from the previous project.

This follow-up does not complete hardware audio recording, count-in, latency
calibration or the wider #2553 cutover.
