# Preferences

Open the Preferences dialog from **Settings > Preferences**.

The dialog is organised into sections; each section is described below.

## General

- **Zoom In Sensitivity** — Controls how fast the timeline zooms in
- **Zoom Out Sensitivity** — Controls how fast the timeline zooms out
- **Shift+Zoom Sensitivity** — Controls zoom speed when holding Shift
- **Default View** — Default visible range when opening a project (in bars)
- **Auto-Save** — Enable or disable automatic saving, and set the interval

## UI

- **Panel visibility defaults** — Choose which panels are shown on startup
- **Behavior settings** — Configure UI interaction preferences
- **Auto-crossfade new audio clips** - New audio clips overlap into a [crossfade](../clips.md#crossfades) automatically when they meet a neighbour
- **New clips play through overlaps** - What a new clip starts with when something covers it. Off, the clip on top owns the span it covers and the clip underneath plays only what is left; on, both sound across the overlap. This is only the starting value, so changing it leaves existing clips alone; per clip, right-click and tick **Play Through Overlap**. See [Overlapping clips](../clips.md#overlapping-clips)
- **New tracks run post-FX after the fader** - The side of the track fader a new track's [post-FX section](../fx-chain.md#post-fx-section) starts on. Existing tracks keep their own setting, which you change from the fader tag on the post-FX panel
- **Duplicate Loop Range: grow / advance** - When you duplicate the loop range, choose whether the loop grows to cover the copy or advances past it
- **Auto-hide arrangement scrollbars** — Keep the arrangement scrollbars hidden until you hover near them, for a cleaner workspace
- **Language** — Select the UI language from the dropdown. The list is populated from the locale files (`.json`) shipped in MAGDA's `lang/` folder, so available languages grow as translations are contributed. A restart-required hint appears after switching; the new language takes effect the next time MAGDA launches. See [Localization](../localization.md) for how translations are managed and how to contribute.

## Appearance

The **Appearance** tab groups everything that controls how MAGDA looks: the colour theme, interface scale, typeface, layout density, and the track/clip colour palettes. Every control here applies **live** the moment you click **OK** or **Apply** - no restart required.

### Theme

Pick MAGDA's colour scheme from the **Application Theme** dropdown. Three themes ship built in:

| Theme | Notes |
|---|---|
| **Dark** | The default. |
| **Light** | Light-background variant. |
| **High Contrast** | Black background with brighter colours throughout, for maximum legibility. |

Two factory themes ship alongside them, listed below a separator: **Concrete Warehouse**, a muted industrial palette with amber accents, and **Neon Cyberpunk**, saturated pinks and cyans on deep violet. They behave exactly like custom themes, so they double as worked examples of the JSON format.

Any custom themes you have installed appear below a further separator, listed by name.

#### Custom themes

Three buttons under the dropdown let you build and manage your own themes:

- **Download Theme Template** - exports the current theme's palette as a complete, editable `.json` file to start from.
- **Load Theme** - pick an edited `.json`; MAGDA copies it into the Themes folder and selects it.
- **Open Themes Folder** - reveals the Themes folder in your file browser.

Themes are plain JSON files in `Documents/MAGDA/Themes`. Each file declares:

- **`name`** - the display name shown in the dropdown.
- **`base`** - either `dark` or `light`. Any colour role you do not override is inherited from that base, so you only need to list what you want to change.
- **`colours`** - a map of colour role to hex value. Roles include a six-step accent ramp (`accent1` is the primary selection/active colour, through `accent6`) plus surface and text roles.
- **`syntaxColours`** - optional; themes every code surface in MAGDA - the DSL console, the AI prompt box, and the Faust source editor - covering editor background, line numbers, caret, selection, and the syntax token colours. Roles you leave out inherit from the base, exactly like `colours`. AI-generated themes always write this section out in full.

The filename (without `.json`) is the theme's id. Unknown keys and invalid colours are skipped, so a partial file is safe to load.

MAGDA watches the selected theme file while it is active: edit its JSON on disk and the change re-applies instantly, which makes hand-tuning a palette quick.

#### Generate a theme with AI

You can also describe a theme in plain language. In the [AI Assistant](../panels/ai-assistant.md#slash-commands) console, type `/theme <description>` - for example `warm sunset, dark` or `cyberpunk neon on black`. MAGDA writes the generated theme to `Documents/MAGDA/Themes`, selects it, and applies it live. Edit the resulting file to fine-tune it.

### UI Scale

Pick a global scale factor for the whole interface. Useful on HiDPI / 4K screens where MAGDA looks too small at native resolution, or on the opposite end where you want more screen real estate.

| Setting | Behavior |
|---|---|
| **Auto (from display DPI)** | Scale is derived from the display's reported DPI. |
| **100% – 200%** | Fixed multiplier, ignoring DPI auto-detection. Common picks: 125% on a regular 4K monitor, 150% on a Retina laptop with external scaling, 200% if you want to see fewer rows from across the room. |

Changes apply live - no restart required. The shortcuts ++cmd+plus++ and ++cmd+minus++ also bump the scale up and down by one preset.

### UI Font

Choose the interface typeface from the **UI Font** dropdown. The first entry, **Inter (default)**, is MAGDA's bundled font; below it every typeface installed on your system is listed alphabetically, so the available fonts depend on your machine. **UI Font** changes the typeface only - use **Font Size** to change how large it renders.

### Font Size

Use **Font Size** to scale MAGDA-owned UI text without changing component geometry. Use **Localized Font Size** as an extra multiplier on top of the global font setting for localized glyphs, so 150% global and 120% localized renders those glyphs at 180%. Chinese defaults to 115% and Japanese defaults to 110% until the user saves their own localized font size.

### Density

The **Spacing** slider tightens or loosens the padding between interface elements, applied on top of the UI Scale. It ranges from **60%** to **140%** in 5% steps, with **100%** the default. Lower values pack more onto the screen; higher values give the layout more breathing room.

### Track & Clip Colours

- **Track Colour Palette** - Define the swatch set offered when you colour tracks and clips
- **Track colours** - Right-click any track header to assign a colour; the colour tints headers in the arrangement, session view, and mixer strips
- **Clip Colours** - Clips can have their own colour or follow the track colour

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

## Rendering

Where rendered files go and what they are called. Audio export format is chosen in the export dialog: WAV 16-bit, WAV 24-bit, WAV 32-bit float, or FLAC.

## Defaults

What a **new** project starts as. Nothing here touches the project you have open: a project's own length, sample rate and bit depths belong to it and are edited in [Project Settings](../arrangement-view.md#project-settings).

- **Default Total Length** — Timeline length a new project starts with (in bars)
- **Sample Rate** — Working and render sample rate for new projects
- **Render Bit Depth** — Bit depth for audio exports
- **Bounce Bit Depth** — Bit depth for bounced and frozen audio

### Credits

The credits that describe *you* rather than the song — Artist, Composer, Songwriter, Producer, Arranger, Genre, Copyright and Website — are copied into every new project, so you fill them in once instead of in every song. Edit them per project in [Project Settings → Metadata](../arrangement-view.md#metadata).

Title, Album, Original Artist, Year and Comment have no default: they belong to the work, and a stored value would be wrong in every project after the first.

## AI

AI provider configuration has moved to a dedicated dialog. Open it from **Settings > AI Settings**.

See [AI Settings](ai-settings.md) for the full reference.

## Shortcuts

Two tabs:

- **Keyboard** — view and remap every keyboard shortcut, with a reset-to-defaults option
- **Gestures** — remap the mouse and trackpad wheel gestures (scroll, zoom, track height) per context

Both are saved between sessions. See [Keyboard Shortcuts](../reference/keyboard-shortcuts.md) for the full default list of keys and gestures.
