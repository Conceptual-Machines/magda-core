# Localization String Extraction Plan

## Infrastructure (DONE)
- `lang/en.json` — master string table (~200 keys)
- `magda/daw/core/StringTable.hpp/.cpp` — singleton loader with `tr("key")` shorthand
- Keys use dot notation: `menu.file.new_project`, `tracks.mute`, etc.

## Phase 1: Menu Strings
**Files:** `magda/daw/ui/windows/MenuManager.cpp`

Replace all hardcoded menu item labels with `tr()` calls. Include `core/StringTable.hpp`.

Example:
```cpp
// Before:
menu.addItem(id, "New Project");
// After:
menu.addItem(id, tr("menu.file.new_project"));
```

All menu keys are under `menu.*` in en.json.

---

## Phase 2: Transport Panel
**Files:** `magda/daw/ui/panels/TransportPanel.cpp`

Replace:
- Count-in menu labels (`"Off"`, `"1 Beat"`, etc.) → `tr("transport.count_in.*")`
- CPU tooltip text → `tr("transport.cpu.*")`
- Button tooltips if any

---

## Phase 3: Panel Tab Names
**Files:** `magda/daw/ui/panels/content/PanelContent.hpp`

The `getContentTypeName()` function returns hardcoded strings. Move it to a `.cpp` file (create `PanelContent.cpp`, add to CMake `magda/daw/CMakeLists.txt`) and replace with `tr("panels.*")` calls.

Also update `getContentTypeIcon()` — though icon names probably don't need translation.

Each content type's `getContentInfo()` override also has hardcoded name/description strings. Update:
- `PianoRollContent.hpp` — `"Piano Roll"`, `"MIDI note editor"`
- `DrumGridClipContent.hpp` — `"Drum Grid"`, `"Drum grid MIDI editor"`
- `TrackChainContent.hpp` — `"Track Chain"`, `"Track signal chain"`
- `WaveformEditorContent.hpp` — `"Waveform"`, `"Waveform editor"`
- `AIChatConsoleContent.hpp` — `"AI Chat"`, `"AI assistant chat"`
- `PluginBrowserContent.hpp` — `"Plugins"`, `"Plugin browser"`
- `MediaExplorerContent.hpp` — `"Samples"`, `"Media explorer"`

---

## Phase 4: Track Headers
**Files:** `magda/daw/ui/components/tracks/TrackHeadersPanel.cpp`

Replace:
- Button labels: `"M"`, `"S"`, `"R"` → `tr("tracks.mute")`, etc.
- Context menu items (right-click on track header)
- Automation button tooltip
- Input monitoring tooltip text

---

## Phase 5: Inspector
**Files:** `magda/daw/ui/panels/content/inspector/TrackInspector.cpp`

Replace section labels:
- `"Routing"` → `tr("inspector.routing")`
- `"Audio"` / `"MIDI"` → `tr("inspector.audio")` / `tr("inspector.midi")`
- `"Sends / Receives"` → `tr("inspector.sends")`
- `"+ Add Send"` → `tr("inspector.add_send")`
- `"No sends"` → `tr("inspector.no_sends")`

---

## Phase 6: Dialog Titles & Messages
**Files:** `magda/daw/ui/windows/MainWindowMenus.cpp`, `magda/daw/ui/dialogs/*.cpp`

Replace:
- `AlertWindow::showMessageBoxAsync` title strings
- Dialog constructor title strings
- Error messages
- Button labels (`"OK"`, `"Cancel"`, `"Apply"`, `"Browse..."`)
- File chooser dialog titles

All keys are under `dialogs.*` in en.json.

---

## Phase 7: Common Strings
**Files:** Various — search for remaining hardcoded strings

Replace scattered strings:
- `"Master"` / `"Master Output"` → `tr("common.master")` / `tr("common.master_output")`
- `"Volume"` / `"Pan"` → `tr("common.volume")` / `tr("common.pan")`
- `"On"` / `"Off"` → `tr("common.on")` / `tr("common.off")`

---

## Phase 8: Language Selector in Preferences
**Files:** `magda/daw/ui/dialogs/PreferencesDialog.cpp`, `magda/daw/core/Config.hpp/.cpp`

Add:
- Language dropdown in Preferences → reads available `lang/*.json` files
- Config stores selected language code
- On change, call `StringTable::getInstance().load(selectedFile)` and trigger UI refresh
- App startup reads config and loads the selected language file

---

## Rules for Implementers

1. **Always include `#include "core/StringTable.hpp"`** in any `.cpp` that uses `tr()`
2. **Never put `tr()` in headers** — it pulls in StringTable.hpp transitively. Keep it in `.cpp` files only.
3. **Keys must exist in en.json** — if you need a new string, add it to en.json first, then use `tr("new.key")`.
4. **Don't translate internal identifiers** — plugin IDs, parameter names from TE, file paths, log messages.
5. **Test with missing keys** — `tr()` returns the key itself as fallback, so typos show up as `menu.file.nwe_project` in the UI.
6. **One phase per commit** — keeps diffs reviewable.
