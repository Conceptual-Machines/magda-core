# Waveform Editor

![Waveform Editor with Clip Properties](../assets/images/panels/audio-editor-properties.png)

Displayed in the bottom panel when an audio clip is selected. Shows the audio waveform with:

- **Zoom and scroll** within the clip
- **Fade handles** at clip edges for fade-in/fade-out
- **Warp markers** for time-stretching (when warp is enabled)
- **Selection** for cutting, copying, or rendering portions
- **Clip properties panel** (right side) — Inline access to audio properties without switching to the Inspector

!!! note "Header controls"
    - **ABS / REL** — Toggle between absolute (timeline) and relative (clip) time mode
    - **Grid resolution** — Draggable numerator/denominator for grid subdivision
    - **SNAP** — Toggle snap-to-grid
    - **GRID** — Toggle grid line visibility

## Edit in an External Editor

To do destructive editing in another application (Audacity, iZotope RX, and so on), right-click an audio clip and choose **Edit in External Editor**. MAGDA hands the clip's source file to the application you have configured, so changes you save there are reflected back in MAGDA.

Set which application to use under **External Audio Editor** in [Preferences](../interface/preferences.md#external-audio-editor). The menu item is disabled until an editor is configured.
