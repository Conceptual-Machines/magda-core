# Troubleshooting

## No Audio Output

1. Check **Settings > Audio & MIDI** and verify the correct driver and output interface are selected, and that the outputs you need are ticked under **Audio Outputs**
2. Make sure your system volume is turned up
3. Check that no tracks are muted and the master fader is up
4. Try a different sample rate (e.g., 44100 Hz or 48000 Hz)

## High Latency

1. Lower the **Buffer Size** in **Settings > Audio & MIDI**
2. Use an ASIO driver (Windows) or Core Audio (macOS) for best performance. On Linux, use JACK (see [Linux drivers](../interface/audio-settings.md#linux-drivers))
3. Close other audio applications

## MAGDA Started With a Different Audio Driver

If MAGDA closed unexpectedly while opening your audio driver, it starts the next time with the default driver instead, and an **Audio Driver Skipped** message names the driver it skipped. Choose a driver again in **Settings > Audio & MIDI**. If the same driver keeps failing, check for an update from its maker.

## Crashes on Startup

1. Reset MAGDA's configuration (see below) — a bad setting, stale audio device, or failed model load can prevent MAGDA from reaching the main window
2. Check the [GitHub Issues](https://github.com/Conceptual-Machines/magda-core/issues) for known problems
3. File a bug report with your system details and the `magda.log` file from the Logs folder

## Plugin Issues

1. Verify the plugin is compatible with your OS and architecture
2. Rescan plugins with **Rescan all** in **Settings > Plugins**. A plugin that crashed a scan is listed under **Excluded** there; **Remove** lets the next scan try it again
3. Try loading the plugin in a fresh project

When a plugin can't be loaded, as you add it or when a project opens, MAGDA shows a warning naming each plugin that failed and why, and the plugin's slot in the device chain shows **Failed to load**. Check that the plugin is still installed and authorised, then reopen the project.

An ARA plugin such as Melodyne needs one **Rescan all** after updating from an older MAGDA before it can be used as ARA. See [Plugins](../interface/plugin-settings.md#scanning).

## Resetting MAGDA

If MAGDA misbehaves and the in-app settings can't help — typically when it crashes before the UI loads (e.g. a bad AI model path or an audio device that's no longer available) — delete the config file manually and MAGDA will start with defaults on the next launch.

### Config file location

| Platform | Path |
|---|---|
| macOS | `~/Library/MAGDA/config.json` |
| Windows | `%APPDATA%\MAGDA\config.json` |
| Linux | `~/.config/MAGDA/config.json` |

The config file stays here even if you move MAGDA's **Data** folder in [Settings > Paths](../interface/preferences.md#paths).

### What gets reset

Deleting `config.json` wipes every user setting: audio device selection, recent projects, browser favourites, AI provider and model settings, keyboard shortcuts, UI language, custom track-colour palette, panel layout, and the "check for updates" state. Your projects (`.mgd` files) and any audio you've rendered are untouched.

On Windows, the uninstaller offers to remove **Settings, logs and caches** (the `%APPDATA%\MAGDA` folder), ticked by default. It keeps `Documents\MAGDA`, where your presets, renders and themes live.

### Logs

MAGDA writes a rolling log to `Logs/magda.log` in its data folder, which is next to the config file unless you have moved it. **Reveal** on the **Data** row in **Settings > Paths** opens that folder. When filing a bug report, attach the log — it usually contains the last few seconds before a crash.

### Waveform peak cache

MAGDA stores generated waveform peak files separately from projects and the configurable MAGDA data folder. These `.mpk` files are used to draw audio waveforms faster and are safe to delete; MAGDA rebuilds them automatically when the source audio is displayed again.

| Platform | Path |
|---|---|
| macOS | `~/Library/magda/peaks` |
| Windows | `%APPDATA%\magda\peaks` |
| Linux | `~/.config/magda/peaks` |

If waveform display looks stale or the cache grows too large, quit MAGDA and delete the `peaks` folder.

### Nuclear option

To wipe everything MAGDA has on disk, delete the `MAGDA` config folder at the platform path above and the generated cache folder listed under **Waveform peak cache**. Project files live elsewhere (wherever you saved them) and are safe.

!!! tip
    If your issue isn't listed here, check the [GitHub Issues](https://github.com/Conceptual-Machines/magda-core/issues) page or open a new issue.
