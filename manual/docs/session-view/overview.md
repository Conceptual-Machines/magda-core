# Session View

The Session view is a clip launcher for real-time performance and idea sketching, similar to Ableton Live's Session view. Switch to it by clicking **Live** in the footer bar.

## Layout

The Session view is organized as a grid:

- **Columns** represent tracks
- **Rows** represent scenes
- Each cell is a **clip slot** that can hold an audio or MIDI clip

```
 Track 1    Track 2    Track 3   │ Scene
┌──────────┬──────────┬──────────┼────────┐
│ Clip A   │          │ Clip D   │ ► 1    │
├──────────┼──────────┼──────────┼────────┤
│          │ Clip B   │ Clip E   │ ► 2    │
├──────────┼──────────┼──────────┼────────┤
│ Clip C   │          │          │ ► 3    │
├──────────┼──────────┼──────────┼────────┤
│ Fader    │ Fader    │ Fader    │ ■ Stop │
└──────────┴──────────┴──────────┴────────┘
```

- **Track headers** run across the top — click to select a track
- **Scene launch buttons** on the right trigger all clips in a row simultaneously
- **Stop All** button stops all playing clips
- **Mini mixer** fader row at the bottom provides quick volume, pan, mute, and solo controls

## Drag and Drop

- Drag audio files from your file browser onto a clip slot to import them
- Drag plugins onto a track header to add effects

## What's Next

- [Clips & Scenes](clips-and-scenes.md) — Triggering clips and launching scenes
- [Mini Mixer](mini-mixer.md) — Per-track fader controls at the bottom
