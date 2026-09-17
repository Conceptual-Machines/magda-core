# Session loop duration and editor position

A Session clip's Arrangement placement is not its playback cycle. Creating a
one-bar MIDI clip and extending its loop to four bars must let playback and the
editor playhead traverse all four bars, including when the edit happens while
the slot is playing. Changing the placement to disguise a duration mismatch is
not a fix: placement, content range, and Session launch state have separate jobs.

## Failure mechanisms

The MIDI editor previously added the clip's placement start to its Session
playhead and passed that fabricated timeline position through the Arrangement
content mapper. That mapper correctly rejects positions past the placement end.
The result was a Session playhead disappearing after the original one bar even
when the slot had a four-bar cycle. Both the ruler and note grid used that path.

There was also a live-edit propagation gap. The snapshot compiler used the new
Session cycle for the material, but the launch handle received its repeat
interval only on launch. Editing a running clip could therefore publish four
bars of material while the handle continued retriggering every bar.

## Ownership contract

- `ClipInfo::sessionCycleBeats` defines a Session pass from the authored loop
  region, with placement as the non-looped/fallback duration.
- Arrangement editor positions remain absolute timeline beats and remain
  bounded by the Arrangement placement.
- Session editor positions come from that particular slot's playback position.
  They map directly through the clip's loop start, phase, and trim; they do not
  acquire an Arrangement placement start or its bounds. An inactive Session
  clip must not display another clip's or the global transport's playhead.
- Each UI timer tick writes its per-clip positions before notifying timeline
  listeners. The ruler calculates its position synchronously in that notification,
  while the note grid reads clip state during a later paint. Notifying before
  updating the clip makes the ruler trail the grid by a timer interval, with an
  especially visible disagreement at a loop wrap.
- The compiled clip snapshot owns the material and its loop configuration.
  The audio callback must use the same snapshot for both configuration adoption
  and material playback. Updating a duration must not require relaunching a
  slot, rebuilding its handle, or adding a UI property listener that sends a
  second interpretation of the model.
- An unchanged configuration must not reset a run or repeatedly override a
  launch gesture. Explicit play/stop requests retain their ordering and
  quantization. Empty recording slots and non-looped material retain their
  separate semantics.

`SessionSlotPlayback::loopBeats` represents repeat configuration explicitly;
positive material length alone does not mean a slot loops. Before advancing
handles, `EngineSession` pins the clip snapshot for the block. Handles adopt a
changed published interval before queued requests are applied, and the sources
then render that same pinned material. Each handle remembers the last adopted
configuration independently of a request override, including an explicitly
non-looping configuration. Re-reading it on later blocks is a no-op. A live
duration change preserves the run and its scheduling origin; shrinking a loop
takes effect at the next boundary on the new interval's grid.

The offline driver follows the same ordering so there is one scheduling
contract, but offline validation does not replace the live checks below.

## Regression and live checks

The editor mapping tests must cover a four-bar loop on a one-bar placement,
nonzero placement start, loop start and MIDI offset, short loops, non-looped
trim, and unchanged Arrangement bounds. Launcher tests must extend the cycle
before launch and during a run, observe progress beyond the original bar, and
observe the eventual wrap at the new boundary.

With `make run-console`, create a one-bar MIDI Session clip, extend its loop to
four bars, and launch it. Watch the piano-roll/drum-grid playhead and ruler pass
bar one and wrap at the four-bar boundary. Repeat while editing a playing clip;
place a note beyond bar one to check that the later material is reached. Repeat
the quantized-stop check from `session-stop-acknowledgement.md`.

These automated checks establish position and scheduling behavior. They do not
establish audible first-hit or stretch quality; live listening remains necessary
for those claims.

## Reproduction evidence (2026-09-17)

Before changing the engine, the new launcher regression failed both checks for
extending a playing clip: the handle never advanced beyond its former four-beat
wrap, and its visible playhead never passed that endpoint. The extension before
launch passed at the engine level, isolating the editor mapping as the other
failure. Existing launch and quantized-stop cases still passed.

The subsequent ruler/grid alignment regression uses the real JUCE timer and a
timeline listener that reads clip state synchronously. With the original timer
order it failed five assertions: first-play callbacks saw no clip position,
later callbacks saw the previous tick, and the wrap callback saw the position
from before the wrap. Publishing the clip positions before notifications fixes
the ordering for both first-play state changes and ordinary position updates.
With the fix restored, all 13 timer-test assertions pass, and the Debug app
rebuild passes. The stopped-cue icon styling is included in that rebuild.

Final automated validation: `make debug` passed; `Slot Launcher Tests` passed
25 cases / 152 assertions, including a verified beat-eight note after extending
the live loop and its eventual sixteen-beat wrap. The standard engine suite
plus editor playhead tests passed 1,254 cases, with the two existing expected
tempo/warp failures. Formatting and diff checks passed. The broader suite needs
access to its temporary audio/configuration files in the macOS cache directory;
the sandboxed attempt could not create those fixtures and was rerun with that
access. The user confirmed the Session fixes, including ruler/grid alignment
and stopped-cue styling, through the application before requesting the PR.
