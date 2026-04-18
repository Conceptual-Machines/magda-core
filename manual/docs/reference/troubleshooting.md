# Troubleshooting

## No Audio Output

1. Check **Settings > Audio** and verify the correct output device is selected
2. Make sure your system volume is turned up
3. Check that no tracks are muted and the master fader is up
4. Try a different sample rate (e.g., 44100 Hz or 48000 Hz)

## High Latency

1. Lower the buffer size in **Settings > Audio**
2. Use an ASIO driver (Windows) or Core Audio (macOS) for best performance
3. Close other audio applications
4. Switch to Live mode for the lowest latency profile

## Crashes on Startup

1. Reset MAGDA's configuration (see below) — a bad preference, stale audio device, or failed model load can prevent MAGDA from reaching the main window
2. Check the [GitHub Issues](https://github.com/Conceptual-Machines/magda-core/issues) for known problems
3. File a bug report with your system details and the `magda.log` file from the Logs folder

## Plugin Issues

1. Verify the plugin is compatible with your OS and architecture
2. Re-scan plugins from **Settings > Plugins**
3. Try loading the plugin in a fresh project

## Resetting MAGDA

If MAGDA misbehaves and the in-app settings can't help — typically when it crashes before the UI loads (e.g. a bad AI model path or an audio device that's no longer available) — delete the config file manually and MAGDA will start with defaults on the next launch.

### Config file location

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/MAGDA/config.json` |
| Windows | `%APPDATA%\MAGDA\config.json` |
| Linux | `~/.config/MAGDA/config.json` |

### What gets reset

Deleting `config.json` wipes every user preference: audio device selection, recent projects, browser favourites, AI provider and model settings, UI language, custom track-colour palette, panel layout, and the "check for updates" state. Your projects (`.mgd` files) and any audio you've rendered are untouched.

### Logs

MAGDA writes a rolling log next to the config file under `Logs/magda.log`. When filing a bug report, attach the log — it usually contains the last few seconds before a crash.

### Nuclear option

To wipe everything MAGDA has on disk (config + logs + cached thumbnails), delete the whole `MAGDA` folder at the platform path above. Project files live elsewhere (wherever you saved them) and are safe.

!!! tip
    If your issue isn't listed here, check the [GitHub Issues](https://github.com/Conceptual-Machines/magda-core/issues) page or open a new issue.
