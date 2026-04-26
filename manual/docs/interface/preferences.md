# Preferences

Open the Preferences dialog from **Settings > Preferences**.

![Preferences](../assets/images/interface/preferences.png)

## General

- **Zoom In Sensitivity** — Controls how fast the timeline zooms in
- **Zoom Out Sensitivity** — Controls how fast the timeline zooms out
- **Shift+Zoom Sensitivity** — Controls zoom speed when holding Shift
- **Default Length** — Default timeline length for new projects (in bars)
- **Default View** — Default visible range when opening a project (in bars)
- **Auto-Save** — Enable or disable automatic saving, and set the interval

## UI

- **Panel visibility defaults** — Choose which panels are shown on startup
- **Behavior settings** — Configure UI interaction preferences
- **Language** — Select the UI language from the dropdown. The list is populated from the locale files (`.json`) shipped in MAGDA's `lang/` folder, so available languages grow as translations are contributed. A restart-required hint appears after switching; the new language takes effect the next time MAGDA launches. See [Localization](../localization.md) for how translations are managed and how to contribute.

### UI Scale

Pick a global scale factor for the whole interface. Useful on HiDPI / 4K screens where MAGDA looks too small at native resolution, or on the opposite end where you want more screen real estate.

| Setting | Behavior |
|---|---|
| **Auto** | Scale is derived from the display's reported DPI. |
| **100% – 200%** | Fixed multiplier, ignoring DPI auto-detection. Common picks: 125% on a regular 4K monitor, 150% on a Retina laptop with external scaling, 200% if you want to see fewer rows from across the room. |

Changes apply live — no restart required. The shortcuts ++cmd+plus++ and ++cmd+minus++ also bump the scale up and down by one preset.

## Colours

- **Custom colour palette** — Define a custom set of colours for tracks and clips
- **Track colours** — Right-click any track header to assign a colour; the colour tints headers in the arrangement, session view, and mixer strips
- **Clip colours** — Clips can have their own colour or follow the track colour

## Rendering

- **Default format** — Choose the default audio format for renders (WAV, AIFF, FLAC, OGG)
- **Sample rate** — Default sample rate for rendered files
- **Bit depth** — Default bit depth for rendered files

## AI

AI provider configuration has moved to a dedicated dialog. Open it from **Settings > AI Settings**.

See [AI Assistant — Setup](../panels/ai-assistant.md#setup) for details.

## Shortcuts

- **Keyboard shortcuts** — View and customize all keyboard shortcuts
- **Reset to defaults** — Restore the default shortcut mappings

See [Keyboard Shortcuts](../reference/keyboard-shortcuts.md) for the full default shortcut list.
