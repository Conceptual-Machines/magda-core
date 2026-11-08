# Plugins

The **Plugins** section of [Settings](preferences.md) manages where MAGDA looks for plugins, scanning, and plugins that are kept out. Folder, format and exclusion changes wait for **Apply**, except that starting a scan applies them first.

## Scanning

The tab row shows how many plugins are available and how many are excluded, with three buttons:

- **Scan for new** — Looks for plugins that have appeared since the last scan, and scans only those.
- **Rescan all** — Scans every plugin in every folder again, with a progress bar.
- **View report** — Opens the log of the most recent scan, showing which plugins were found, loaded, or rejected.

While a scan runs, **Stop scan** ends it.

**Scan for new plugins on startup** checks for new plugins each time MAGDA starts. It is off by default.

Plugins whose files have disappeared are dropped from the list on startup, with one exception: if the missing file lived on an **external or network volume that is not currently mounted**, the entry is kept. An unplugged drive is not evidence that you uninstalled the plugin, so the entry is waiting for you when you plug it back in. This covers `/Volumes/...` on macOS, drive letters and UNC shares on Windows, and `/media`, `/run/media`, and `/mnt` on Linux.

!!! note "ARA plugins after updating"
    MAGDA hosts ARA plugins such as Melodyne only if the scan recorded them as ARA. Plugin lists scanned by older versions of MAGDA don't record this, for VST3 or Audio Unit, so after updating, click **Rescan all** once before inserting an ARA plugin. See [ARA Plugins](../ara.md).

## Plugin folders

The folders MAGDA scans. Your operating system's standard VST3 folders, and on macOS its Audio Unit folders, are always listed, marked **System**. On macOS these are:

- `~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3`
- `~/Library/Audio/Plug-Ins/Components` and `/Library/Audio/Plug-Ins/Components` (Audio Units)

**+ Add folder…** adds a folder of your own, and **Remove** beside it takes it off the list.

## Preferred plugin format

**When a plugin exists in several formats** offers **Prefer VST3**, **Prefer LV2**, and on macOS **Prefer AU**. When the same plugin is installed in more than one format, the browser shows it once, in the format you prefer; a plugin that doesn't come in that format keeps the formats it has.

Projects you have already saved still load whichever exact plugin they referenced, regardless of this setting.

## Excluded

Plugins that crashed or failed to load during a scan are listed here with the reason and date, and later scans skip them. **Remove** on a row lets the next scan try that plugin again; **Reset All** clears the whole list.

A plugin that fails to load when you add it or open a project shows **Failed to load** in its device slot, and MAGDA names it, with the reason, in a warning. See [Troubleshooting](../reference/troubleshooting.md#plugin-issues).
