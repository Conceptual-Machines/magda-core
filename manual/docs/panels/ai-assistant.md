# AI Assistant

MAGDA includes a built-in AI chat assistant that lets you control the DAW using natural language, and a DSL console for direct scripting.

![AI Assistant](../assets/images/panels/ai-assistant.png){ width="400" }

## Overview

The AI Assistant panel is located in the left panel. It has two tabs at the bottom: **AI** for natural-language interaction and **DSL** for direct script execution.

## AI Tab

Type a request in natural language and the assistant translates it into actions:

- "Add a MIDI track with a bass clip"
- "Transpose the selected notes up an octave"
- "Set the tempo to 120 BPM"
- "Mute tracks 3 and 4"

The assistant is **context-aware** — it knows which tracks, clips, and devices exist in your project and what is currently selected.

### How It Works

1. You type a natural-language request in the chat
2. The assistant translates your request into MAGDA's internal DSL (domain-specific language)
3. The DSL commands are executed as actions in the project
4. The assistant confirms what was done

### Setup

The AI Assistant uses the OpenAI API. You'll need an API key to get started.

1. Go to **Settings > Preferences > AI**
2. Enter your OpenAI API key

If you don't have an API key, you can generate one at [platform.openai.com/api-keys](https://platform.openai.com/api-keys).

### Usage Tips

- Be specific: "Add a reverb to Track 2" works better than "make it sound spacey"
- The assistant can handle multi-step requests: "Create 4 MIDI tracks and name them Kick, Snare, HiHat, Bass"
- Use it for repetitive tasks: "Set all tracks to -6 dB"
- Prefix a message with `/dsl` to execute DSL directly from the AI chat without making an AI call

## DSL Tab

The DSL tab provides a code editor with **syntax highlighting** for the MAGDA DSL. It's designed for users who want to script DAW operations directly without going through the AI.

### Editor Features

- **Syntax highlighting** — keywords (blue), methods (yellow), parameters (light blue), strings (orange), numbers (green), note names (teal), comments (green)
- **Direct execution** — commands run immediately against the DAW with no network calls
- **Command history** — results appear in the output area above the editor
- **Keyboard shortcuts**:

| Shortcut | Action |
|----------|--------|
| ++cmd+enter++ (Mac) / ++ctrl+enter++ (Win) | Execute code |
| ++cmd+l++ (Mac) / ++ctrl+l++ (Win) | Clear output |

### Quick Start

Switch to the DSL tab, type a command, and press ++cmd+enter++:

```
track(name="Bass", new=true).clip.new(bar=1, length_bars=4)
```

Type `help` and execute to see available commands.

## MAGDA DSL Reference

The MAGDA DSL is a functional scripting language for DAW operations. Both the AI assistant and the DSL tab use it — the AI generates it from natural language, while the DSL tab lets you write it directly.

### Tracks

```
track(name="Bass")                        // Reference or create track by name
track(name="Bass", new=true)              // Always create a new track
track(id=1)                               // Reference existing track by index
track(name="Bass").track.set(volume_db=-3, pan=0.5, mute=true, solo=true)
track(name="Bass").select()               // Select track in UI
track(name="Bass").delete()               // Delete track
```

### Clips

```
track(name="Bass").clip.new(bar=1, length_bars=4)     // Create MIDI clip at bar 1
track(name="Bass").clip.new(length_bars=4)             // Auto-place after last clip
track(name="Bass").clip.rename(index=0, name="Intro")  // Rename clip
track(name="Bass").clip.delete(index=0)                 // Delete clip
```

### Notes

```
.notes.add(pitch=C4, beat=0, length=1, velocity=100)
.notes.delete()
.notes.transpose(semitones=5)
.notes.set_pitch(pitch=F1)
.notes.set_velocity(value=80)
.notes.quantize(grid=0.25)       // 0.25=16th, 0.5=8th, 1.0=quarter
.notes.resize(length=0.5)
```

Pitch accepts note names (`C4`, `F#3`, `Bb2`) or MIDI numbers.

### Chords

```
.notes.add_chord(root=C4, quality=major, beat=0, length=1, velocity=100, inversion=0)
```

Supported qualities: `major`, `minor`, `dim`, `aug`, `sus2`, `sus4`, `dom7`, `maj7`, `min7`, `dim7`, `dom9`, `maj9`, `min9`, `dom11`, `min11`, `dom13`, `min13`, `add9`, `add11`, `add13`, `6`, `min6`, `7b5`, `7sharp5`, `7b9`, `7sharp9`, `min7b5`, `power`

Inversions: `0` = root position, `1` = first inversion, `2` = second inversion.

### Arpeggios

```
.notes.add_arpeggio(root=C4, quality=minor, beat=0, step=0.5,
                    note_length=0.5, velocity=100, pattern=up, fill=true)
```

| Parameter | Description |
|-----------|-------------|
| `step` | Beat interval between notes |
| `note_length` | Duration of each note (defaults to `step`) |
| `pattern` | `up`, `down`, or `updown` |
| `fill` | `true` to repeat the pattern to fill the entire clip |
| `beats` | Fill a specific number of beats instead of the whole clip |

### Effects

```
track(name="Vocals").fx.add(name="reverb")
track(name="Vocals").fx.add(name="Pro-Q 3", format="VST3")
```

Built-in effects: `eq`, `compressor`, `reverb`, `delay`, `chorus`, `phaser`, `filter`, `utility`, `pitch shift`, `ir reverb`

Third-party plugins are referenced by name. Use `format="VST3"` or `format="AU"` to disambiguate if needed.

### Groove/Shuffle/Swing

```
// Create a custom groove template
groove.new(name="Funky 16ths", notesPerBeat=4,
           shifts="0.0,0.15,-0.05,0.4,0.0,0.2,-0.1,0.35")

// Extract groove from an audio clip's transients
groove.extract(clip=0, resolution=16, name="Drum Loop Feel")

// Apply groove to a MIDI clip
groove.set(template="Basic 8th Swing", strength=0.8)

// List all available groove templates
groove.list()
```

| Command | Description |
|---------|-------------|
| `groove.new` | Create a custom groove template from lateness values |
| `groove.extract` | Extract groove from audio clip transients |
| `groove.set` | Set groove template and strength on current MIDI clip |
| `groove.list` | Show all available groove templates |

| Parameter | Description |
|-----------|-------------|
| `name` | Template name |
| `notesPerBeat` | Grid resolution: `2` = 8th notes, `4` = 16th notes (default: `2`) |
| `shifts` | Comma-separated lateness values (`-1.0` to `1.0`, where `0` = on grid) |
| `parameterized` | `true` if strength slider should scale the groove (default: `true`) |
| `resolution` | For extract: `8` or `16` (default: `16`) |
| `template` | For set: groove template name |
| `strength` | For set: groove amount `0.0`–`1.0` |

### Selection and Filtering

```
// Select notes by condition
.notes.select(note.pitch == C4)
.notes.select(note.velocity > 100)

// Select clips by condition
.clips.select(clip.length_bars >= 2)

// Bulk operations on matching tracks
filter(tracks, track.name == "Drums").track.set(mute=true)
filter(tracks, track.name == "Drums").for_each(.fx.add(name="compressor"))
```

### Comments

```
// This is a comment — everything after // is ignored
track(name="Bass")  // Inline comments work too
```

### Example: Full Workflow

```
// Create a synth track with a 4-bar chord progression
track(name="Synth", new=true)
  .clip.new(bar=1, length_bars=4)
  .notes.add_chord(root=C4, quality=major, beat=0, length=4)
  .notes.add_chord(root=F4, quality=major, beat=4, length=4)
  .notes.add_chord(root=G4, quality=major, beat=8, length=4)
  .notes.add_chord(root=A3, quality=minor, beat=12, length=4)

// Add an arpeggiated lead
track(name="Lead", new=true)
  .clip.new(bar=1, length_bars=4)
  .notes.add_arpeggio(root=C4, quality=major, step=0.25, pattern=updown, fill=true)

// Add reverb and set volume
track(name="Lead").fx.add(name="reverb").track.set(volume_db=-6)

// Add swing to the lead
groove.set(template="Basic 8th Swing", strength=0.6)
```
