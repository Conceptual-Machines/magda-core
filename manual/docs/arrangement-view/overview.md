# Arrangement View

The Arrangement view is a traditional DAW timeline for composing, recording, and editing. Switch to it by clicking **Arrange** in the footer bar.

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
- **Master track** (bottom) — Master output with its own resizable row (40–150 px).

## Toolbar

The corner toolbar provides quick access to:

- Zoom to fit / Zoom to selection / Zoom to loop
- Track size presets (small, medium, large)
- Add track
- I/O visibility toggle

## What's Next

- [Tracks & Clips](tracks-and-clips.md) — Working with tracks and clips on the timeline
- [Timeline Navigation](timeline-navigation.md) — Zooming, scrolling, and selection
- [Editing](editing.md) — Split, join, duplicate, and render
