# Settings

All of MAGDA's settings live in one **Settings** window. Open it with **MAGDA > Settings…** on macOS, or **Edit > Settings…** on Windows and Linux. The shortcut is ++cmd+comma++ (++ctrl+comma++ on Windows and Linux).

Only one Settings window opens at a time. Asking for it again brings it to the front on the section you asked for. Some controls elsewhere open it on the right page: the controller, Ableton Link and local-model pills in the [footer](overview.md#footer), for example.

## The window

The sidebar on the left lists the sections. A section with more than one page shows its pages as tabs along the top, and a tab's own buttons sit at the right of the tab row.

| Section | What it covers | Documented in |
|---|---|---|
| **General** | Zoom, timeline, transport, auto-save, engine, layout, behaviour and language | [General](#general) |
| **Appearance** | Theme, scale, fonts, spacing and colours | [Appearance](#appearance) |
| **Audio & MIDI** | Audio driver and interface, channels, MIDI ports | [Audio & MIDI](audio-settings.md) |
| **Controllers** | MIDI controller profiles and Lua scripts | [Controllers](controllers.md) |
| **Connections** | MCP, WebSocket, OSC, remote clients, Ableton Link, MIDI clock | [Connections](connections.md) |
| **Plugins** | Plugin folders, scanning, excluded plugins | [Plugins](plugin-settings.md) |
| **AI** | Assistant setup, API keys, local models | [AI](ai-settings.md) |
| **Rendering** | Where exports start and how rendered files are named | [Rendering](#rendering) |
| **Paths** | Data, presets, capture and media database folders, external editor | [Paths](#paths) |
| **Defaults** | What a new project starts with | [Defaults](#defaults) |
| **Shortcuts** | Keyboard shortcuts and mouse gestures | [Shortcuts](#shortcuts) |

### Search

The search box at the top of the sidebar (++cmd+f++ / ++ctrl+f++) filters every section and tab by the text of their controls. Sections with nothing matching drop out of the sidebar. ++escape++ clears the search, or closes the window when the search is already empty.

Search becomes available a moment after the window opens, once every section has finished loading.

### Applying changes

Most sections keep your edits until you apply them. Each section shows how many settings you have changed beside its name in the sidebar, and the footer shows the total.

| Button | What it does |
|---|---|
| **Restore defaults** | Shows the default values for the section you are in, as pending changes. Nothing is saved until you apply. |
| **Cancel** | Closes the window and drops every change you have not applied (++escape++). |
| **Apply** | Saves the pending changes in every section and keeps the window open. |
| **OK** | Applies and closes (++return++). |

**Audio & MIDI**, **Controllers** and **Connections** are different: each edit there takes effect as you make it. With one of them selected, the footer says so and offers only **Done**.

A few actions in the other sections also act at once rather than waiting for **Apply**: downloading or removing a model and changing a model folder under **AI**, and starting a plugin scan, which applies the **Plugins** section's pending changes first.

## General

General is one scrolling page, in groups.

### Zoom

- **Zoom In Sensitivity** / **Zoom Out Sensitivity** — How fast the timeline zooms in and out.
- **⇧+Zoom Sensitivity** — Zoom speed while ++shift++ is held.

### Timeline

- **New Project View** — How many bars a new project shows when it opens (32 by default). This is how much is on screen, not how long the project is: that is **Length** under [Defaults](#defaults).

### Transport

- **Stop updates playhead** — When on, **Stop** leaves the playhead where playback stopped, so the next **Play** carries on from there. Off by default: **Play** starts again from where the playhead was.
- **Follow playhead during playback** — Scroll the arrangement to keep the playhead in view. On by default.

### Auto-Save

- **Enable auto-save** — On by default.
- **Interval** — How often, from 10 to 300 seconds (60 by default).

#### Recovering unsaved work

After a crash or forced stop, MAGDA offers the previous session's unsaved project once.
The offer shows its name, creation and edit times, track and clip counts, and MAGDA version.
Choose **Later** to keep it under **File > Recover Unsaved Project**, where you can recover or discard it.
A normal quit, including **Don't Save**, retires that session's autosave.

Saved projects offer a newer recovery snapshot when you reopen the project after an unclean exit.
**Cancel** leaves both the current project and the snapshot intact.
Recovery files are kept for 30 days, together with any temporary media they need.
Snapshots from other builds are available in the recovery menu without a startup prompt.

Use **Open Older Autosave** in the recovery menu to select an `.autosave` file from an older MAGDA version.
Creation dates that were not recorded by older versions appear as **Unknown**.

### Engine

- **Render playback ahead of the audio device** — Renders playback ahead of the audio device by the **Look-Ahead** time (5 to 200 ms, 20 by default). Off by default.
- **Keep what you play, so it can be captured afterwards** — Turns on the capture buffer behind **Transport > Recent Capture** (the **MEM** switch in the transport). See [Capturing what you just heard](../recording-comping.md#capturing-what-you-just-heard).
    - **Capture Buffer** — How much is kept, shown with what it costs: 5 minutes by default, up to 4 hours. Anything past the first minute is kept on disk, in the **Capture history** folder under [Paths](#paths).
    - **Capture Length** — The most a capture selects for you, in bars (8 by default).
- **Unload plugins when freezing, to free their memory** — When you freeze a track, the plugins rendered into its frozen audio are unloaded. On by default. See [Freezing Tracks](../tracks.md#freezing-tracks).

### Layout

- **Headers on the Right** — Put the track headers on the right of the arrangement. Also in the **View** menu.
- **Auto-Hide Main View Scrollbars** — Keep the scrollbars hidden until you hover near them.
- **Compact / Medium / Large Track Height** — The three heights the track-height buttons beside the arrangement's vertical scrollbar switch between.
- **Narrow / Medium / Wide Track Width** — The three channel-strip widths in the [Mixer](../mixer-view.md).

### Behavior

- **Confirm before deleting tracks** — On by default.
- **Auto-monitor selected track** — Selecting a track sets its input monitor to Auto. See [Input Monitoring](../tracks.md#input-monitoring). Off by default.
- **Duplicate Loop Range grows the loop (off: advances it)** — When you duplicate the loop range, the loop grows to cover the copy; off, it moves on to the copy. On by default.
- **Show tooltips** — On by default.
- **Show info line in the footer** — Name the control under the mouse, with its help text and shortcut, on the left of the [footer](overview.md#footer). On by default.
- **Open plugin window when added to a chain** — Off by default.
- **Add a separator after a new chord track** — On by default.
- **BEAT without a detected tempo uses the project tempo** — A clip with no tempo of its own can go into beat mode without you typing a BPM. On by default.

The four **New projects:** switches set what a new project starts with. A project keeps the values it was created with, so changing them here leaves existing projects alone.

- **New projects: chord preview** — Whether the chord track auditions chords. Off by default.
- **New projects: auto-crossfade** — Whether new audio clips get an automatic [crossfade](../clips.md#crossfades) where they meet a neighbour. On by default.
- **New projects: overlap plays both** — Whether a new clip plays through clips that overlap it. Off, the clip on top owns the span it covers. Per clip, right-click and tick **Play Through Overlap**. See [Overlapping clips](../clips.md#overlapping-clips). Off by default.
- **New projects: post-FX after fader** — Which side of the fader a new track's [post-FX section](../fx-chain.md#post-fx-section) starts on. Existing tracks keep their own setting. On by default.

### Language

- **UI Language** — The languages MAGDA ships with, listed by language code. The new language is loaded when you apply, and a hint reminds you that a restart is needed for it to apply everywhere. See [Localization](../localization.md).
- **Localized Font Size** — An extra size multiplier for localized text, on top of **Font Size** under [Appearance](#appearance): 150% there and 120% here renders localized glyphs at 180%. Chinese defaults to 115% and Japanese to 110% until you set your own.

## Appearance

Everything that controls how MAGDA looks. Changes take effect when you click **Apply** or **OK**, with no restart.

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

Three buttons in the Theme group let you build and manage your own themes:

- **Get template** - exports the current theme's palette as a complete, editable `.json` file to start from.
- **Load theme** - pick an edited `.json`; MAGDA copies it into the Themes folder and selects it.
- **Show folder** - reveals the Themes folder in your file browser.

Themes are plain JSON files in `Documents/MAGDA/Themes`. Each file declares:

- **`name`** - the display name shown in the dropdown.
- **`base`** - either `dark` or `light`. Any colour role you do not override is inherited from that base, so you only need to list what you want to change.
- **`colours`** - a map of colour role to hex value. Roles include a six-step accent ramp (`accent1` is the primary selection/active colour, through `accent6`) plus surface and text roles.
- **`syntaxColours`** - optional; themes every code surface in MAGDA - the AI prompt box and the Faust source editor - covering editor background, line numbers, caret, selection, and the syntax token colours. Roles you leave out inherit from the base, exactly like `colours`. AI-generated themes always write this section out in full.

The filename (without `.json`) is the theme's id. Unknown keys and invalid colours are skipped, so a partial file is safe to load.

MAGDA watches the selected theme file while it is active: edit its JSON on disk and the change re-applies instantly, which makes hand-tuning a palette quick.

#### Generate a theme with AI

You can also describe a theme in plain language. In the [AI Assistant](../panels/ai-assistant.md#slash-commands) console, type `/theme <description>` - for example `warm sunset, dark` or `cyberpunk neon on black`. MAGDA writes the generated theme to `Documents/MAGDA/Themes`, selects it, and applies it live. Edit the resulting file to fine-tune it.

### Display Scale

- **UI Scale** — A scale factor for the whole interface. **Auto (from display DPI)** follows the display; **100%** to **200%** (in 25% steps) is a fixed multiplier. Useful on HiDPI and 4K screens where MAGDA looks too small at native resolution. ++cmd+plus++ and ++cmd+minus++ (++ctrl++ on Windows and Linux) step the scale up and down from anywhere in MAGDA.
- **UI Font** — The interface typeface. **Inter (default)** is MAGDA's bundled font; below it, every typeface installed on your system is listed alphabetically. This changes the typeface only.
- **Font Size** — Scales MAGDA's own text without changing the size of the controls, from 80% to 150%.
- **Spacing** — Tightens or loosens the padding between interface elements, on top of the UI scale: 60% to 140%, 100% by default. Lower values pack more onto the screen.

### Controls

- **Track colour** — How track headers show the track's colour: **Spine**, a stripe on the outer edge, or **Full bar**, the header's top bar.
- **Mute / solo** — Draw the track header's mute and solo buttons as **Icons** or as **Letters (M / S)**.

### New-Project Track Palette

The swatches offered when you colour tracks and clips, up to 16. Click a swatch to edit its hex value and name below the grid, **+ Add Colour** to add one, and **Remove** to delete the selected one.

The palette is copied into each new project, so editing it here leaves your open project alone. Tick **Apply palette to current project too** to update the open project as well when you apply.

### New-Project Clip Colours

- **New-project clip colour** — Whether new clips **Inherit from track** or **Cycle through palette**.

To colour a track, right-click its header; the colour tints its header in the arrangement, Session view and mixer strips.

## Rendering

Where rendered files go and what they are called. The audio format is chosen in the export dialog: WAV, FLAC, Opus or MP3, with a bit depth for WAV and FLAC.

### Output folder

- **Export dialog opens in** — **Documents**, or a **Custom folder** you pick with **Choose…**. This is only where the export dialog starts. Bounces always go to the project's renders folder.

### File names

The **Export** and **Bounce** fields set the pattern for each kind of file, with a preview of the resulting name underneath. Click a chip under **Insert** to add `clip-name`, `track-name`, `project-name` or `date-time` to the field you are editing. Typing adds plain text, ++backspace++ removes the last character or chip, and clicking a chip in the field removes it.

| Pattern | Default |
|---|---|
| **Export** | `<project-name>_<date-time>` |
| **Bounce** | `<clip-name>_<date-time>` |

## Paths

Where MAGDA keeps its files. Each folder row shows the path in use, with **Change…** to pick another folder, **Reveal** to open it in your file browser, and **Use default** once you have moved it.

### Folders

| Folder | Holds | Default |
|---|---|---|
| **Data** | Logs, scripts, plugin caches | `~/Library/MAGDA` (macOS), `%APPDATA%\MAGDA` (Windows), `~/.config/MAGDA` (Linux) |
| **Presets** | Chains, racks, devices | `Documents/MAGDA/Presets` |
| **Capture history** | Recordings kept by the capture buffer | The system's temporary folder |
| **Render output** | Set under [Rendering](#rendering) | |

When you change the **Data** or **Presets** folder, MAGDA asks whether to copy the existing files to the new location. A new **Data** folder is marked **Restart to apply**: when you click **OK** or **Apply**, MAGDA asks you to save any unsaved changes and then quits, so start it again to use the new folder. A new **Presets** folder needs no restart.

MAGDA's settings file, `config.json`, stays in the default data location wherever you move the **Data** folder. See [Resetting MAGDA](../reference/troubleshooting.md#resetting-magda).

### Media database

The location and maintenance of the sample-indexing database: **Location**, a count of indexed files and of those with audio embeddings, **Index presets**, and **Wipe database…**. See [Media Library — Managing the database](../panels/media-library.md#managing-the-database).

### External audio editor

The application MAGDA hands audio clips to when you choose **Edit in External Editor** (see [Waveform Editor](../panels/waveform-editor.md#edit-in-an-external-editor)). Use **Choose app…** to pick it, or **Remove** to clear it. While no editor is set, choosing the clip menu item shows an alert asking you to pick one.

## Defaults

What a **new** project starts with. Nothing here touches the project you have open: a project's own length, sample rate and bit depths belong to it and are edited in [Project Settings](../arrangement-view.md#project-settings).

### New projects

- **Length** — The timeline length a new project starts with, in bars.
- **Sample rate** — The working and render sample rate. If your audio device runs at a different rate, a note under the setting says so.
- **Bit depth for renders** — 16-bit, 24-bit or 32-bit float, for audio exports.
- **Bit depth for bounces** — The same choices, for bounced and frozen audio. 32-bit float keeps headroom for re-processing.

### Credits

The credits that describe *you* rather than the song — Artist, Composer, Songwriter, Producer, Arranger, Genre, Copyright and Website — are copied into every new project, so you fill them in once instead of in every song. Artist and genre also go into the tags of rendered files. Edit them per project in [Project Settings → Metadata](../arrangement-view.md#metadata).

Title, Album, Original Artist, Year and Comment have no default: they belong to the work, and a stored value would be wrong in every project after the first.

## Shortcuts

Two tabs, each with **Reset all** in the tab row. Edits on both wait for **Apply**, like the rest of the window.

- **Keyboard** — Every command that can take a shortcut, by category, with its keys drawn as key caps. Click a cap to record a new key, **Not set** to add one, and right-click a cap to clear it. A key that two commands share is shown in amber on both, with **Also used by** naming the other. Type in the filter box to find a command by name, or press a shortcut there to find what it does.
- **Gestures** — The mouse wheel, trackpad and drag gestures for each part of MAGDA (arrangement, piano roll, drum grid, waveform, and so on). Each row shows its input; click **Learn** and perform a gesture to change it, and set its **Sensitivity** and **Invert** where they apply.

See [Keyboard Shortcuts](../reference/keyboard-shortcuts.md) for the full default list of keys and gestures.
