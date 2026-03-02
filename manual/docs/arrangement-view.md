# Arrangement View

The Arrangement View is a traditional DAW timeline for composing, recording, and editing. Switch to it by clicking **Arrange** in the footer bar.

![Arrangement View](assets/images/arrangement/arrangement-view.png)

## Layout

```
┌─────────────────────────────────────────────┐
│ Timeline ruler (bars / beats / time)        │
├──────────┬──────────────────────────────────┤
│ Track    │                                  │
│ Headers  │   Track Content (clips on        │
│          │   the timeline)                  │
│  Name    │                                  │
│  Color   │         ▼ Playhead               │
│  M S R   │                                  │
├──────────┼──────────────────────────────────┤
│ Aux      │   Aux track content              │
├──────────┼──────────────────────────────────┤
│ Master   │   Master waveform                │
└──────────┴──────────────────────────────────┘
```

- **Track headers** (left) — Track name, color, mute/solo controls. Resizable width.
- **Track content** (center) — Clips displayed on the timeline. Scrollable horizontally and vertically.
- **Timeline ruler** (top) — Shows bars, beats, and time markers.
- **Playhead** — Vertical line showing the current playback position, always on top.
- **Aux tracks** — Auxiliary/bus tracks in a fixed section above the master.
- **Master track** (bottom) — Master output with its own resizable row.

## Toolbar

The corner toolbar provides quick access to:

- Zoom to fit / Zoom to selection / Zoom to loop
- Track size presets (small, medium, large)
- Add track
- I/O visibility toggle

## Working with Clips

- **Move** — Click and drag a clip along the timeline or between tracks
- **Resize** — Drag the edges of a clip to trim it
- **Select** — Click a clip to select it; ++ctrl+a++ (++cmd+a++) to select all
- **Delete** — Select a clip and press ++delete++ or ++backspace++
- **Duplicate** — ++ctrl+d++ (++cmd+d++) to duplicate selected clips

Drag audio files from the [Media Explorer](panels/browsers.md) directly onto a track to import them as clips.

## Timeline Navigation

### Scrolling

- **Horizontal scroll** — Move along the timeline (mouse wheel or scroll gesture)
- **Vertical scroll** — Navigate between tracks

### Zooming

- **Horizontal zoom** — ++ctrl+plus++ / ++ctrl+minus++ (++cmd+plus++ / ++cmd+minus++ on macOS) or use the zoom scrollbar
- **Vertical zoom** — Change track height using the toolbar presets (small, medium, large)

### Toolbar Zoom Actions

- **Zoom to fit** — Fit all content in the visible area
- **Zoom to selection** — Zoom into the current time selection
- **Zoom to loop** — Zoom to the loop region

### Selection

- **Time selection** — Click and drag on the timeline ruler to select a time range
- **Loop region** — Displayed as an overlay on the timeline; set from selection with ++ctrl+shift+l++ (++cmd+shift+l++)

### Playhead

The playhead shows the current playback position. Click on the timeline ruler to reposition it. Press ++home++ to return to the start.

## Editing Modes

MAGDA supports two editing modes depending on context:

### Clip-Based Editing

When no time selection is active, editing operations apply to **selected clips**:

- **Split** (++cmd+e++) — Splits selected clips at the edit cursor position
- **Render** (++cmd+b++) — Renders selected clips to audio in place
- **Cut / Copy / Paste / Duplicate / Delete** — Operate on the selected clips

### Time Selection Editing

When a time selection is active (drag on the timeline ruler), operations apply to the **time range**:

- **Split / Trim** (++cmd+e++) — Splits clips at the selection boundaries and isolates the selected region. If clips extend beyond the selection, they are trimmed.
- **Render Time Selection** (++cmd+shift+b++) — Consolidates everything within the time selection to a single audio clip per track
- **Cut / Copy / Paste** — Operate on the content within the time range

!!! note
    When a time selection exists, Split/Trim affects all clips that overlap the selection — not just selected clips. Clear the time selection to return to clip-based editing.

## Editing

### Cut, Copy, Paste

| Action | Shortcut |
|--------|----------|
| Cut | ++ctrl+x++ / ++cmd+x++ |
| Copy | ++ctrl+c++ / ++cmd+c++ |
| Paste | ++ctrl+v++ / ++cmd+v++ |

### Split and Join

- **Split** — Position the edit cursor and press ++ctrl+e++ (++cmd+e++) to split the clip at that point. If a time selection is active, clips are trimmed to the selection.
- **Join** — Select adjacent clips and press ++ctrl+j++ (++cmd+j++) to merge them.

### Duplicate

- **Duplicate clips** — ++ctrl+d++ (++cmd+d++) duplicates selected clips
- **Duplicate track** — ++ctrl+d++ with a track selected duplicates the track and its content
- **Duplicate track (empty)** — ++ctrl+shift+d++ (++cmd+shift+d++) duplicates the track structure without content

### Render

- **Render clip to audio** — Select a clip and press ++ctrl+b++ (++cmd+b++) to bounce it to an audio file
- **Render time selection** — Press ++ctrl+shift+b++ (++cmd+shift+b++) to render the selected time range

### Undo / Redo

| Action | Shortcut |
|--------|----------|
| Undo | ++ctrl+z++ / ++cmd+z++ |
| Redo | ++ctrl+shift+z++ / ++cmd+shift+z++ |
