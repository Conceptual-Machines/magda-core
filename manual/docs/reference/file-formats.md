# File Formats

## Project Files

MAGDA saves projects in Tracktion Engine's Edit format (`.tracktionedit`), an XML-based format that stores:

- Track layout and settings
- Clip references and positions
- Plugin chains and parameters
- Automation data

## Audio Formats

MAGDA supports the following audio formats:

| Format | Import / preview | Export |
|--------|------------------|--------|
| WAV | Yes | Yes |
| AIFF / AIF | Yes | No |
| FLAC | Yes | Yes |
| OGG | Yes | No |
| MP3 | Yes, where a platform decoder is available | No |

Audio export offers WAV 16-bit, WAV 24-bit, WAV 32-bit float, and FLAC. MP3 is import-only and MAGDA does not encode MP3 files.

## MIDI

Standard MIDI files (`.mid`) can be imported and exported.

## Generated Cache Files

MAGDA generates waveform peak caches when audio files are displayed. These are not project data; they are derived from source audio and can be deleted safely.

| Format | Location | Description |
|---|---|---|
| `.mpk` | OS application data directory, under `magda/peaks` | High-resolution waveform peak cache. Each file stores min/max peak data for one source audio file and is regenerated automatically when needed. |

MAGDA validates `.mpk` files against the source audio file size and modification time. If the source audio changes, the old cache is ignored and a new one is built.

Default peak-cache paths:

| Platform | Path |
|---|---|
| macOS | `~/Library/magda/peaks` |
| Windows | `%APPDATA%\magda\peaks` |
| Linux | `~/.config/magda/peaks` |

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
