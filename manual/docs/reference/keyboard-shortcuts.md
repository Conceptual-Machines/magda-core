# Keyboard Shortcuts

Everything below is the **default** mapping, with ++cmd++ on macOS and ++ctrl++ on Windows and Linux. You can remap any shortcut in [Settings > Shortcuts](../interface/preferences.md#shortcuts), on the **Keyboard** tab, and reset them all with **Reset all**. Mouse and trackpad gestures are remappable on the **Gestures** tab of the same section — see [Mouse & Trackpad Gestures](#mouse-trackpad-gestures). Your changes are saved when you apply them.

## Transport

| Shortcut | Action |
|----------|--------|
| ++space++ | Play / Stop |
| ++c++ | Metronome on / off |
| ++cmd+m++ | Add a marker at the playhead |
| ++cmd+alt+arrow-left++ / ++cmd+alt+arrow-right++ | Previous / next marker |
| ++bracket-left++ / ++bracket-right++ | Move the playhead to the loop start / end |
| ++shift+bracket-left++ / ++shift+bracket-right++ | Move the playhead to the selection start / end |
| ++cmd+shift+c++ | Recent Capture |

## Editing

| Shortcut | Action |
|----------|--------|
| ++cmd+z++ | Undo |
| ++cmd+shift+z++ | Redo |
| ++cmd+x++ | Cut |
| ++cmd+c++ | Copy |
| ++cmd+v++ | Paste |
| ++cmd+d++ | Duplicate selected clips, track, or chain devices |
| ++cmd+alt+shift+d++ | Duplicate clips with the automation under them |
| ++cmd+a++ | Select all in the focused panel (see [Panel focus](../interface/overview.md#panel-focus)) |
| ++delete++ / ++backspace++ | Delete selected clips, track, or chain devices |
| ++shift+arrow-left++ / ++shift+arrow-right++ | Nudge selected clips one grid step earlier / later |
| ++shift+arrow-up++ / ++shift+arrow-down++ | Move selected clips to the track above / below |
| ++cmd+e++ | Split / trim at cursor or selection |
| ++cmd+j++ | Join clips |
| ++cmd+b++ | Render clip to audio |
| ++cmd+shift+b++ | Render time selection |
| ++q++ | Quantize the selected notes, or every note of the selected MIDI clips, to the grid |
| ++cmd+l++ | Loop the selected clip on / off |
| ++0++ | Enable / disable the selected clips |
| ++e++ | Show / hide the gain envelope on the selected audio clips |
| ++cmd+shift+l++ | Set the loop from the selected clip |
| ++escape++ | Exit link mode / clear edit cursor |

## Tracks

| Shortcut | Action |
|----------|--------|
| ++cmd+t++ | Add new track |
| ++cmd+shift+t++ | Add new group track |
| ++cmd+shift+d++ | Duplicate track without content |
| ++cmd+alt+d++ | Duplicate track content only |
| ++m++ | Toggle mute on selected track |
| ++shift+s++ | Toggle solo on selected track |

## View

| Shortcut | Action |
|----------|--------|
| ++f1++ / ++f2++ / ++f3++ | Live / Arrange / Mix view |
| ++tab++ / ++shift+tab++ | Next / previous view |
| ++h++ / ++g++ | Zoom the arrangement in / out |
| ++t++ | Open the arrangement's edit tools at the mouse |
| ++cmd+shift+p++ | Piano roll fullscreen on / off |
| ++cmd+plus++ / ++cmd+minus++ | Make the whole interface larger / smaller (UI scale) |

## File and Settings

| Shortcut | Action |
|----------|--------|
| ++cmd+s++ | Save |
| ++cmd+shift+s++ | Save as |
| ++cmd+comma++ | Open [Settings](../interface/preferences.md) |

## Arrangement keys

These keys are built into the arrangement and are not listed on the Shortcuts tab.

| Shortcut | Action |
|----------|--------|
| ++1++ to ++5++ | Pick an edit tool: Pointer, Pencil, Slice, Glue, Erase. Hold the key to use the tool only while it is held |
| ++l++ | Set the loop from the time selection, or from the selected clip |
| ++cmd+0++ | Zoom to fit the timeline |

## Mouse Shortcuts

| Shortcut | Action |
|----------|--------|
| ++cmd++ + click (++ctrl++ + click on Windows/Linux) | Toggle clip, track, chain, or device selection |
| ++shift++ + click | Range-select clips, tracks, chains, or devices |
| ++shift+ctrl++ + click | Erase arrangement clip under cursor |
| ++alt++ + drag a device | Copy the device instead of moving it |

## Mouse & Trackpad Gestures

Wheel, trackpad and drag gestures are context-aware and remappable in **Settings > Shortcuts > Gestures**. The defaults (use ++ctrl++ for ++cmd++ on Windows/Linux):

| Gesture | Action |
|---------|--------|
| Wheel | Scroll vertically |
| ++shift++ + wheel | Scroll horizontally |
| ++cmd++ + wheel | Zoom horizontally toward the cursor |
| ++alt++ + wheel | Adjust track height (arrangement) |
| Trackpad horizontal swipe | Scroll horizontally |
| Drag up or down on the ruler | Zoom horizontally (++shift++ faster, ++alt++ finer) |
| ++alt++ + drag a clip | Copy the clip instead of moving it |
| ++alt+shift++ + drag a clip | Make a [ghost copy](../clips.md#linked-ghost-clips) of the clip |

In the **piano roll**, ++alt++ + wheel changes note-row height. In the **drum grid**, ++alt++ + wheel changes row height and ++cmd++ + wheel zooms the timebase.

In the **MIDI editors**, ++shift++ + wheel over a note nudges its velocity, taking over from horizontal scroll while a note is hovered or a selection exists.
