# Coordinate System: Beats Are the Domain

## Rule

MAGDA's UI domain is **beats**, not seconds. The zoom unit is **pixels per beat (ppb)**. All UI coordinate math (pixel positioning, grid drawing, clip bounds, selections) must work in beats natively.

**Seconds are only used at the boundary** — when talking to the audio engine, file I/O, or external APIs. Never convert seconds→beats→pixels when a beat value is already available.

## Why

Converting seconds→beats introduces floating point drift: `startTime * BPM / 60.0` does not always equal `startBeats` exactly. When this drifted value is multiplied by zoom and truncated to int, the pixel position shifts differently at different zoom levels. This causes clips to visually appear at different ruler positions depending on zoom — a recurring bug that has been fixed multiple times.

## How to Apply

### Clip positioning
Always use `startBeats` / `lengthBeats` for pixel calculations. Fall back to seconds conversion only if beat values are not set (< 0):

```cpp
double beats = (clip->startBeats >= 0.0) ? clip->startBeats : clip->startTime * BPM / 60.0;
int pixelX = beatsToPixel(beats);
```

### Pixel conversion functions
- `beatsToPixel(double beats)` / `pixelToBeats(int pixel)` — primary, no conversion
- `timeToPixel(double seconds)` / `pixelToTime(int pixel)` — convenience wrappers that convert through beats

### State values
Many state values (editCursorPosition, selection times, etc.) are stored in seconds for historical reasons. When converting these to pixels, use `timeToPixel()` which goes through beats internally. But when beat-domain equivalents exist (e.g., `startBeats`, `lengthBeats`, `selection.startBeats`), prefer those.

### New code
When adding new UI positioning code, always ask: "do I have this value in beats already?" If yes, use `beatsToPixel()` directly. Never compute `seconds * BPM / 60` when a beat value is sitting right there.
