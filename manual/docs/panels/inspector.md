# Inspector

The Inspector panel is on the right side of the window. It displays context-sensitive properties for the currently selected item.

![Inspector — Audio Clip](../assets/images/panels/inspector-audio.png)

## Track Inspector

Displayed when a track is selected. Shows:

- **Track name** — Click to rename
- **Track color** — Automatically assigned
- **Volume and pan** — Numeric readouts and controls
- **Input routing** — Select audio/MIDI input source
- **Output routing** — Select audio/MIDI output destination
- **Sends** — List of send slots with level controls

## Clip Inspector

Displayed when a clip is selected. Shows:

- **Position** — Start time and length on the timeline
- **Loop settings** — Loop on/off, loop start, loop length
- **Warp** — Time-stretch mode and settings (audio clips)
- **Pitch** — Transpose and fine-tune (audio clips)
- **Fades** — Fade-in and fade-out length and curve
- **Session launch properties** — Trigger mode, quantize, follow action (Session View clips)

## Note Inspector

Displayed when one or more MIDI notes are selected in the Piano Roll. Shows:

- **Pitch** — MIDI note number and name
- **Velocity** — Note velocity (0–127)
- **Start** — Note start position
- **Length** — Note duration

When multiple notes are selected, the inspector shows ranges (e.g., "C3–G5") and allows batch editing.

## Device Inspector

Displayed when a device (plugin or built-in) is selected in the track chain. Shows:

- **Device name** — Click to rename the instance
- **Device type** — Plugin format and category
- **Parameters** — All exposed parameters with knobs/sliders
