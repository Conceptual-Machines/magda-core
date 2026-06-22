# Automation

Automation is the third member of MAGDA's modulation family, alongside [Modulators](modulation/modulators.md) and [Macros](modulation/macros.md). Where modulators and macros generate or proxy a value continuously, automation **records** parameter changes into a lane and replays them in sync with the transport.

Any parameter that has a slider, knob, or fader in MAGDA can be automated.

## Automation Lanes

A lane is a curve drawn underneath a track in the [Arrangement View](arrangement-view.md) that controls a single parameter over time. The parameter follows the lane's value during playback.

To open a lane:

- Right-click any parameter slider → **Show Automation Lane**, or
- Right-click an automation-aware control (macro knob, LFO Rate slider) and pick the same item.

The lane appears below the track. Drag points to edit the curve; double-click to add or remove points; right-click points for shape options.

Multiple lanes can stack on the same track — one per automated parameter — and modulators, macros, and automation can all drive the same parameter at once. Their effects combine.

A device parameter gets its lane from the same place you link modulation: right-click the parameter and choose **Show Automation Lane**. (Parameters on a post-FX device cannot be automated, so the item is disabled there.)

## Curve Shapes

A segment is the line between two points. Right-click a segment to change its shape:

| Shape | Behaviour |
|---|---|
| **Linear** | Straight line between the two points |
| **Bezier** | Smooth curve; drag the handles to shape it |
| **Step** | Holds the first value, then jumps at the next point |
| **Hard Corner** | Two straight segments meeting at a sharp, draggable apex |

## Editing Points

- **Add / remove a point** — double-click in the lane.
- **Inline value** — right-click a point to type its value directly, in the lane's real units (dB, pan %, Hz, …). Press ++enter++ to commit.
- **Point Inspector** — select one or more points to open the inspector, which shows the target name and point count, an editable **Value** field, and the **Position** (beat) of a single selected point. Editing the value across a multi-selection applies the change as a delta, preserving the relative shape.

## Lane Header Controls

Each lane header carries a row of toggles:

| Control | Function |
|---|---|
| **Snap edits to beat grid** | Snap point positions to the bar/beat grid while editing |
| **Snap values to parameter grid** | Snap point values to the parameter's natural steps |
| **Automation on/off** | Bypass the lane without deleting it; the parameter ignores the curve while off |
| **Delete automation lane** | Remove the lane and its curve |

## Master Automation

The master channel has its own automation band above the master strip. Click the **Automation** button in the master header (or right-click the header) to open the master automation menu, where you can show or hide existing master lanes, hide them all, or **Add New Lane** for:

- **Track Volume** — the master output level
- **Tempo** — see below
- any parameter of a device on the master chain, plus master macros and modulators

++alt++-click the Automation button to toggle every automation lane in the edit on or off at once.

These master lanes include **edit-scoped targets** like tempo and master volume that aren't tied to a specific device.

## Tempo Automation

Tempo can be automated over time as a curve in the master automation band. Open the master automation menu, choose **Add New Lane → Tempo**, and the tempo lane appears in the master band. Edit it like any other lane — drag points, set curve shapes, type exact BPM values via the point inspector. The curve drives the project's tempo, so the song speeds up and slows down along the curve during playback.

## Automation Modes

The transport bar shows the automation icon ![automation icon](assets/icons/automation.svg){ width="14" } next to the play/stop controls, with a single letter under it indicating the active mode:

| Letter | Mode |
|---|---|
| (none) | Off — read-only playback |
| **W** | Write |
| **T** | Touch |
| **L** | Latch |

Click the icon to cycle through the modes. The choice controls **how parameter movements during playback are written into a lane**.

| Mode | Behavior |
|---|---|
| **Off** | No recording. Lanes play back; user gestures don't write anything. |
| **Write** | Record every parameter change while the transport is rolling. The classic behavior: anything you touch becomes part of the lane. |
| **Touch** | Record only while you're physically holding the control. Release the mouse and the lane's existing curve takes over again. |
| **Latch** | Same as Touch on the way in: recording starts when you grab the control. After release, the last value you held is **latched** and continues writing into the lane until the transport stops. |

### When to use which

- **Write** for a first pass on an empty lane — sketching the shape from scratch.
- **Touch** for nudging a single section of an existing curve without erasing the parts you don't touch.
- **Latch** for "set and hold" gestures — sweep a filter to its destination, release, and the rest of the lane is rewritten at that target value.
- **Off** when you've finished writing and want playback only. Same effect as a per-DAW "Read" mode.

## Touch and the Override Highlight

In Touch and Latch modes, the parameter you're holding paints a coloured highlight around its slider. That highlight is your visual cue that the parameter is currently **overriding** the lane — you, not the curve, are in charge of that value right now. When you release:

- **Touch** drops the highlight and lane playback resumes.
- **Latch** keeps the highlight until the transport stops; the held value continues to be written.

## Recording Tips

- Automation only records while the transport is **playing**. Moving a control with playback stopped is not captured.
- Each playback pass creates one undoable group — if a take goes wrong, ++cmd+z++ removes the whole pass at once.
- The lane stays visible after stop. You can keep editing it manually with the curve editor.

## Interaction with Modulation

A lane can target a parameter that is also being driven by a modulator or macro. The values are summed:

```
final value = automation lane value + modulator output + macro output
```

This means a slow LFO can wobble around an automation curve, or a macro can offset the whole lane up and down — same parameter, three sources, all live.

## Duplicating Clips with Automation

When you duplicate a clip, you can decide whether its automation comes along. Right-click a clip and choose **Duplicate With Automation** to carry the lane data into the copy, or **Duplicate Without Automation** to leave it behind. See [Clips — Editing Clips](clips.md#editing-clips).

## Removing Automation

To remove a single point: right-click → **Delete Point**.
To clear the whole lane: right-click in the lane → **Clear Lane**.
To hide the lane (keeping the curve): right-click the lane header → **Hide Lane**. Show it again from the parameter's right-click menu.
