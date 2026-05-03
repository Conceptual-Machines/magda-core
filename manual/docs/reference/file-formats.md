# File Formats

## Project Files

MAGDA saves projects in Tracktion Engine's Edit format (`.tracktionedit`), an XML-based format that stores:

- Track layout and settings
- Clip references and positions
- Plugin chains and parameters
- Automation data

## Audio Formats

MAGDA supports the following audio formats for import and export:

| Format | Import | Export |
|--------|--------|--------|
| WAV | Yes | Yes |
| AIFF | Yes | Yes |
| FLAC | Yes | Yes |
| OGG | Yes | Yes |
| MP3 | Yes | No |

## MIDI

Standard MIDI files (`.mid`) can be imported and exported.

## Presets

| Format | Scope | Description |
|---|---|---|
| `.mps` | Device or track chain | MAGDA's native preset format. Captures parameter values, plugin state, macros, modulators, sidechain wiring, and gain. Plugin-aware — loading onto a slot whose plugin id doesn't match is rejected. |
| `.vstpreset` | Single VST3 plugin | The standard VST3 preset format. MAGDA scans the OS's standard preset directories and exposes them in the device header. |
| `.aupreset` | Single Audio Unit plugin | The standard AU preset format. Same scan behaviour as VST3 presets. |

See [FX Chain — Presets](../fx-chain.md#presets) for the workflow.

## Controller Profiles

| Format | Description |
|---|---|
| `.json` | Hardware controller profile — describes the layout (which CCs map to macros, faders, transport buttons). See [Controller Profile Format](controller-profile-format.md) for the schema. |
| `.lua` | Lua 5.4 controller script. See [Lua Scripting](lua-scripting.md). |
