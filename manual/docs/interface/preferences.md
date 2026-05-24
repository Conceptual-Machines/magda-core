# Preferences

Open the Preferences dialog from **Settings > Preferences**.

The dialog is organised into sections; each section is described below.

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

## Storage

MAGDA keeps user data in three configurable folders. Each can be redirected to any path on disk — point them at an external drive, a synced folder, or a per-project staging area.

| Folder | Holds | Default location |
|---|---|---|
| **Data** | App config, controller profiles, Lua scripts, locale overrides | `~/Library/MAGDA` (macOS), `%APPDATA%\MAGDA` (Windows), `~/.config/MAGDA` (Linux) |
| **Presets** | `.mps` device presets and the per-plugin preset cache | `<Data>/presets` |
| **Render** | Bounce / freeze / export output | `<Data>/render` |

Click **Browse…** next to a path to relocate that folder. MAGDA does not move existing content for you — copy or symlink the contents over before switching if you want to keep what's there.

### External Audio Editor

Set the application MAGDA hands audio clips to when you choose **Edit in External Editor** (see [Waveform Editor](../panels/waveform-editor.md#edit-in-an-external-editor)). Use **Browse…** to pick the application, or **Reset** to clear it. While no editor is set, the clip menu item stays disabled.

### Media Database

The location and maintenance of the sample-indexing database live here too. See [Media Library — Managing the database](../panels/media-library.md#managing-the-database).

## Colours

- **Custom colour palette** — Define a custom set of colours for tracks and clips
- **Track colours** — Right-click any track header to assign a colour; the colour tints headers in the arrangement, session view, and mixer strips
- **Clip colours** — Clips can have their own colour or follow the track colour

## Rendering

- **Sample rate** — Default sample rate for rendered files
- **Export bit depth** — Default bit depth for audio exports
- **Bounce bit depth** — Default bit depth for bounced and frozen audio
- Audio export format is chosen in the export dialog: WAV 16-bit, WAV 24-bit, WAV 32-bit float, or FLAC

## AI

AI provider configuration has moved to a dedicated dialog. Open it from **Settings > AI Settings**.

See [AI Settings](ai-settings.md) for the full reference.

## Shortcuts

- **Keyboard shortcuts** — View and customize all keyboard shortcuts
- **Reset to defaults** — Restore the default shortcut mappings

See [Keyboard Shortcuts](../reference/keyboard-shortcuts.md) for the full default shortcut list.
