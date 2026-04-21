# Issue #1054 — QWERTY MIDI keyboard not routing on Windows/Linux

Scratch notes for the agent continuing this on a Windows/Linux machine. Delete this file before opening the fix PR.

## Symptom
- User toggles the QWERTY keyboard in the transport panel
- User selects "QWERTY Keyboard" as the track's MIDI input
- Key presses produce no MIDI at the instrument (4OSC, etc.)
- **Confirmed on Windows AND Linux. Works on macOS.**
- Not a translation-bug cascade (dispatch uses numeric device IDs, not display strings).

## Code map

| Step | File:line |
|------|-----------|
| Key → MIDI note | `magda/daw/audio/QwertyMidiKeyboard.cpp:101` (`sendNoteOn`) |
| Virtual MIDI device (lazy create) | `magda/daw/audio/AudioBridge.cpp:692` (`getQwertyMidiDevice`) |
| Toggle wiring (UI → `setEnabled`) | `magda/daw/ui/windows/MainWindow.cpp:205` (`onQwertyKeyboardToggled`) |
| Track input routing | `magda/daw/audio/AudioBridge.cpp:1213` (`setTrackMidiInput`) |
| Input selector dispatch (numeric ID) | `magda/daw/ui/panels/content/inspector/TrackInspector.cpp:1549` |
| TE virtual MIDI impl | `third_party/tracktion_engine/.../tracktion_VirtualMidiInputDevice.cpp:117` |
| TE device creation | `third_party/tracktion_engine/.../tracktion_DeviceManager.cpp:756` (`createVirtualMidiDevice`) |

## Why macOS works, Win/Linux don't (hypothesis)

No platform `#ifdef`s in the MAGDA chain. Pure C++ dispatch. Likely a timing/ordering issue around TE's `InputDeviceInstance` list propagation that fires reliably on macOS but not elsewhere.

The virtual MIDI device is **lazily created on first QWERTY toggle** (not at startup). Once created, `rescanMidiDeviceList()` runs — but the existing `EditPlaybackContext`'s list of `InputDeviceInstance`s may not get refreshed. Then when the user selects QWERTY in the track input menu, `setTrackMidiInput` loops `playbackContext->getAllInputs()` looking for the device and finds nothing — silently fails to wire.

## First things to check under debugger

1. **In `VirtualMidiInputDevice::sendMessageToInstances`** (`tracktion_MidiInputDevice.cpp:1630`):
   Is `instances` empty when a keypress arrives? If empty → "wasted message" warning path. That confirms the routing never happened.
2. **In `AudioBridge::setTrackMidiInput`** (non-"all" branch, `AudioBridge.cpp:1355+`):
   Does the loop over `playbackContext->getAllInputs()` find an `inputDeviceInstance` where `&instance->owner == midiDevice`? If no, `setTarget` is never called.
3. **Device ID flow**: log the device ID the track input menu passes into `setTrackMidiInput`. Verify it matches the QWERTY device's ID, not something else.
4. **`isPlaybackGraphAllocated()`**: confirm it's `true` when QWERTY is toggled. If `false`, routing is deferred — check whether the deferred path ever runs.

## Speculative fixes to try (in order)

1. **Force playback context reallocate after device creation.** In `getQwertyMidiDevice`, after `createVirtualMidiDevice(...)` succeeds, call `edit.getCurrentPlaybackContext()->reallocate()` to force the input device instance list to refresh. If the bug is context staleness, this resolves it.
2. **Eager device creation at startup**, not lazy-on-toggle. Creates the device before any playback context exists, so the context is born knowing about it. Changes order of operations enough that it might sidestep the bug.
3. **Explicit `createInstance()` call** after device creation if TE doesn't auto-create one for virtual devices in existing contexts.

## What NOT to do

- Don't refactor the QWERTY → MIDI dispatch path on speculation. Repro first, then fix.
- Don't add platform `#ifdef`s. If the bug is in TE/JUCE, the fix belongs there (and should be upstreamed or patched via tracktion_engine submodule), not papered over in MAGDA.

## Verifying the fix

- macOS: QWERTY still works (don't regress).
- Linux: `aconnect -l` in a terminal while MAGDA is running should show the QWERTY virtual MIDI port. `aseqdump -p <port>` should show note-on/note-off as keys are pressed. If messages are visible but the track doesn't hear them, the bug is in the TE dispatch side, not the device creation.
- Windows: same check via loopMIDI or similar tool, or use MIDI-OX to monitor.
- Reporters to ping once an RC is out: @Zloyparen (Windows), @mikobuntu (Linux).

## Context

- Branch base: `main` at merge of #1055 (dynamic transport panel). Commit `a38500c3`.
- Related issue: #1053 (transport panel icon missing — shipped in `v0.5.2-rc0`).
- Reporter on #1053 also saw this bug once the icon became visible.
