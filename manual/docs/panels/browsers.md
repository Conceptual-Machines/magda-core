# Plugin & Media Browser

The left panel contains two browser tools for finding and adding content to your project.

## Plugin Browser

![Plugin Browser](../assets/images/panels/plugin-browser.png)

The Plugin Browser shows all available audio plugins organized in a tree view.

### Navigation

- **Tree view** — Plugins organized by manufacturer and category
- **Search** — Type to filter by plugin name
- **Favorites** — Star plugins for quick access

### Adding Plugins

- **Drag and drop** — Drag a plugin from the browser onto a track header or into the FX chain to add it
- **Double-click** — Add the plugin to the currently selected track's FX chain

### Supported Formats

- VST3
- Audio Units (AU) — macOS only
- VST (legacy)

### Rescanning

If a newly installed plugin doesn't appear, use **Settings > Plugin Scan** to re-scan your plugin directories.

## Media Explorer

![Sample Browser](../assets/images/panels/sample-browser.png)

The Media Explorer lets you browse files on your system and preview audio before importing.

### Navigation

- **File browser** — Navigate your file system with a tree view
- **Path bar** — Click to jump to a specific directory

### Preview

- Click an audio file to preview it through your monitor output
- Preview plays in sync with the transport tempo when applicable

### Filters

- Filter by file type (audio, MIDI, project)
- Sort by name, date, or size

### Importing

- **Drag and drop** — Drag files from the explorer onto a track or clip slot to import
- Supported audio formats: WAV, AIFF, FLAC, OGG, MP3
- Supported MIDI formats: .mid files

### Multi-Select

- ++shift++-click to extend the selection to a contiguous range
- ++cmd++-click (++ctrl++-click on Windows/Linux) to toggle individual files in and out of the selection
- Drag any file in the selection to drag the whole selection out

Drop targets vary by destination:

- [Arrangement View](../arrangement-view.md#multi-sample-drag-and-drop) — empty area creates one track per sample; existing track appends clips sequentially
- [Session View](../session-view.md#multi-sample-drag-and-drop) — empty area creates one track per sample stacked into scene rows; existing track stacks clips down consecutive scene slots
- [Drum Grid](../devices/drum-grid.md#multi-sample-drop) — samples fill consecutive pads from the drop target
