# Session quantized-stop acknowledgement (#2552)

A requested stop and an observed stopped slot are different states. During a
quantized stop, `activeSessionClipId` is cleared immediately because the user
no longer intends that clip to resume. Its engine handle may still be playing
until the boundary. A UI state tick must not interpret that old playing tap as
a follow action and restore the cleared intent.

The pending stop belongs to a particular clip on a track and its monotonic due
beat. Inactive sibling slots cannot acknowledge it. A stopped tap from before
the request cannot acknowledge it before the due beat either. A missing or
moved clip, or a removed handle, must not leave a permanent pending indicator.

While a quantized stop is pending, the track remains in Session mode. An
unrelated clip or scene launch may synchronize playback modes for every track;
that must not switch this track to Arrangement early. Once the target tap
acknowledges the stop, the pending indicator clears and the track returns to
Arrangement. A new launch on the track supersedes the pending stop. Stopping
the transport clears the pending stop without restoring its cleared launch
intent on restart.

Pending stops also count as Session activity for the playhead and launch
quantization. Clearing intent must not hide a still-playing clip's playhead or
make the next launch bypass quantization as though no Session run existed.
When a replacement launch is queued, the old clip's still-playing tap must not
overwrite the replacement's intent. Follow actions can still transfer intent
after the previous clip stops.

The slot icon distinguishes a remembered launch intent from running transport.
While stopped, a cued or selected clip shows a neutral grey play icon and does
not blink. Starting transport restores the existing blue selected/playing/queued
states. Changing selection must not hide another slot's remembered launch cue.

Stop acknowledgement is host-side state reconciliation. Its pending indicator
remains specific to quantized stops. Live loop-duration publication and editor
position mapping have separate contracts described in `session-loop-duration.md`.

## Verification

The `Slot Launcher Tests` JUCE suite drives the host launcher and native engine
together. Regression cases must poll state before and after callbacks and cover
inactive sibling slots, unrelated track launches, the stop boundary, queued
launch cancellation, moved clips, follow-action adoption, and transport
stop/restart. Existing Session restart and launch tests must remain green.

For a live check with `make run-console`, set a Session clip to one-bar launch
quantization, start it, and press an empty slot partway through the bar. The
stop indicator should blink until the boundary, the clip should keep sounding
until then, and the track should return to Arrangement afterwards. Repeat with
other occupied slots on that track and while launching another track. Stop and
restart transport during the pending stop: the explicitly stopped clip should
not relaunch.

## Validation (2026-09-17)

The `Slot Launcher Tests` suite passes 25 cases / 152 assertions, including the
existing launch and transport-restart cases. The new regressions reproduced
pending-stop and Arrangement-return failures against the original launcher.
The fixture now delivers the same synchronous transport-start notification as
the application, and checks slot state rather than mistaking a synth release
tail for a new launch. `make debug`, `make release`, formatting, and diff checks
pass. The user confirmed that stopping works in the application. Their next
report concerned extending a MIDI Session loop; that separate position/configuration
contract is documented in `session-loop-duration.md`. The user also confirmed
the ruler/grid alignment and grey stopped-cue styling after live testing.
