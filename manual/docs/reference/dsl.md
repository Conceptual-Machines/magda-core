# MAGDA DSL Reference

The MAGDA DSL is a functional scripting language for DAW operations. The [AI Assistant](../panels/ai-assistant.md) translates natural language into DSL, and the [DSL tab](../panels/ai-assistant.md#dsl-tab) lets you write it directly.

## Syntax

Commands are written one per line. Track statements return a context that can be extended with a method chain. Comments start with `//`.

```
// This is a comment
track(name="Bass", new=true)
  .clip.new(bar=1, length_bars=4)
  .notes.add_chord(root=C4, quality=major, beat=0, length=4)
```

## Tracks

### Creating & referencing tracks

| Command | Description |
|---------|-------------|
| `track()` | Create a new unnamed track |
| `track(name="Bass")` | Reference existing track by name, or create if none exists |
| `track(name="Bass", new=true)` | Always create a new track (even if name exists) |
| `track(id=1)` | Reference track by 1-based index |

### Modifying tracks

```
track(name="Bass").track.set(name="Sub Bass", volume_db=-3, pan=0.5, mute=true, solo=true)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | string | Rename the track |
| `volume_db` | number | Volume in dB |
| `pan` | number | Pan position (-1.0 left to 1.0 right) |
| `mute` | bool | Mute the track |
| `solo` | bool | Solo the track |

### Other track operations

```
track(id=1).select()    // Select track in UI
track(id=1).delete()    // Delete track
```

## Clips

### Creating clips

```
track(name="Bass").clip.new(bar=1, length_bars=4)     // At specific bar
track(name="Bass").clip.new(length_bars=4)             // Auto-place after last clip
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `bar` | number | auto | Bar position (omit to auto-place) |
| `length_bars` | number | 4 | Clip length in bars |

### Managing clips

```
track(name="Bass").clip.rename(index=0, name="Intro")  // Rename clip at index
track(name="Bass").clip.rename(name="Part {i}")         // Rename selected clips (auto-number)
track(name="Bass").clip.delete(index=0)                 // Delete clip at index
```

### Selecting clips

```
track(id=1).clips.select(clip.length_bars >= 2)
track(id=1).clips.select(clip.start_bar == 1)
```

| Field | Description |
|-------|-------------|
| `clip.length_bars` | Clip length in bars |
| `clip.start_bar` | Clip start position in bars |

Operators: `==`, `!=`, `>`, `>=`, `<`, `<=`

## Notes

Note operations require a selected clip. Chain them on a `clip.new()` call or use a track reference when a clip is already selected.

### Adding notes

```
.notes.add(pitch=C4, beat=0, length=1, velocity=100)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pitch` | note/number | — | Note name (`C4`, `F#3`, `Bb2`) or MIDI number |
| `beat` | number | — | Beat position within the clip |
| `length` | number | 1 | Duration in beats |
| `velocity` | number | 100 | Velocity (0–127) |

!!! tip
    To add multiple notes to the **same clip**, chain `.notes.add()` calls on a single statement. Do **not** create a separate `clip.new()` for each note.

### Adding chords

```
.notes.add_chord(root=C4, quality=major, beat=0, length=1, velocity=100, inversion=0)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `root` | note | — | Root note |
| `quality` | string | — | Chord quality (see table below) |
| `beat` | number | 0 | Beat position |
| `length` | number | 1 | Duration in beats |
| `velocity` | number | 100 | Velocity |
| `inversion` | number | 0 | 0 = root, 1 = first, 2 = second |

**Chord qualities:**

| Category | Qualities |
|----------|-----------|
| Triads | `major` / `maj`, `minor` / `min`, `dim`, `aug`, `sus2`, `sus4`, `power` / `5` |
| Sevenths | `dom7` / `7`, `maj7`, `min7`, `dim7`, `min7b5` / `half_dim` |
| Extended | `dom9` / `9`, `maj9`, `min9`, `dom11` / `11`, `min11`, `maj11`, `dom13` / `13`, `min13`, `maj13` |
| Added tone | `add9`, `add11`, `add13`, `madd9` |
| Sixth | `6` / `maj6`, `min6` |
| Altered | `7b5`, `7sharp5`, `7b9`, `7sharp9` |

### Adding arpeggios

```
.notes.add_arpeggio(root=C4, quality=major, beat=0, step=0.5,
                    note_length=0.5, velocity=100, inversion=0, pattern=up, fill=true)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `root` | note | — | Root note |
| `quality` | string | — | Same chord qualities as `add_chord` |
| `beat` | number | 0 | Start beat |
| `step` | number | — | Beat interval between notes |
| `note_length` | number | = step | Duration of each note |
| `velocity` | number | 100 | Velocity |
| `inversion` | number | 0 | Chord inversion |
| `pattern` | string | `up` | `up`, `down`, or `updown` |
| `fill` | bool | false | Repeat pattern to fill the entire clip |
| `beats` | number | — | Fill a specific number of beats instead of the whole clip |

### Selecting notes

```
.notes.select(note.pitch == C4)
.notes.select(note.velocity > 100)
.notes.select(note.start_beat >= 4)
.notes.select(note.length_beats < 0.5)
```

| Field | Description |
|-------|-------------|
| `note.pitch` | Note pitch (note name or MIDI number) |
| `note.velocity` | Note velocity |
| `note.start_beat` | Note start position in beats |
| `note.length_beats` | Note length in beats |

### Editing notes

All editing operations apply to currently selected notes.

```
.notes.delete()                    // Delete selected notes
.notes.transpose(semitones=5)      // Transpose (positive=up, negative=down)
.notes.set_pitch(pitch=F1)         // Set absolute pitch
.notes.set_velocity(value=80)      // Set velocity
.notes.quantize(grid=0.25)         // Quantize (0.25=16th, 0.5=8th, 1.0=quarter)
.notes.resize(length=0.5)          // Set note length in beats
```

## Effects

```
track(name="Vocals").fx.add(name="reverb")
track(name="Vocals").fx.add(name="Pro-Q 3", format="VST3")
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | string | Effect name |
| `format` | string | Optional: `VST3`, `AU`, or `VST` to disambiguate |

**Built-in effects:** `eq`, `compressor`, `reverb`, `delay`, `chorus`, `phaser`, `filter`, `utility`, `pitch shift`, `ir reverb`

Third-party plugins are referenced by their scanned name. Use the `@alias` syntax in the AI chat to autocomplete plugin names.

## Filter Operations

Bulk operations on tracks matching a condition.

```
filter(tracks, track.name == "Drums").track.set(mute=true)
filter(tracks, track.name == "Drums").delete()
filter(tracks, track.name == "Drums").select()
filter(tracks, track.name == "Drums").for_each(.clip.new(bar=1, length_bars=4))
filter(tracks, track.name == "Drums").for_each(.fx.add(name="reverb").track.set(volume_db=-6))
```

## Groove / Swing

Groove templates control playback timing feel — they shift note positions at playback without modifying the source MIDI. These are **not** drum patterns.

In the AI chat, prefix your message with `/groove` to ensure the AI generates groove commands instead of note patterns.

### Creating custom grooves

```
groove.new(name="Funky 16ths", notesPerBeat=4,
           shifts="0.0,0.15,-0.05,0.4,0.0,0.2,-0.1,0.35")
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | string | — | Template name |
| `notesPerBeat` | number | 2 | Grid resolution: `2` = 8th notes, `4` = 16th notes |
| `shifts` | string | — | Comma-separated lateness values (`-1.0` to `1.0`, `0` = on grid) |
| `parameterized` | bool | true | Whether the strength slider scales the groove |

The `shifts` string defines one value per grid position in a single beat. Positive values push notes **late** (behind the beat), negative values push notes **early** (ahead of the beat).

### Extracting groove from audio

```
groove.extract(clip=0, resolution=16, name="Drum Loop Feel")
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `clip` | number | — | Clip index on the current track |
| `resolution` | number | 16 | Grid resolution: `8` or `16` |
| `name` | string | "Extracted Groove" | Template name |

Reads transient positions from an audio clip and computes timing deviations from the grid.

### Applying groove to a clip

```
groove.set(template="Basic 8th Swing", strength=0.7)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `template` | string | — | Groove template name |
| `strength` | number | — | Amount of groove to apply (`0.0`–`1.0`) |

Applies to the currently selected MIDI clip.

### Listing available grooves

```
groove.list()
```

Returns all available groove template names (built-in and custom).

## Examples

### Simple track with clip

```
track(name="Bass", new=true).clip.new(bar=1, length_bars=4)
```

### Chord progression

```
track(name="Synth", new=true)
  .clip.new(bar=1, length_bars=4)
  .notes.add_chord(root=C4, quality=major, beat=0, length=4)
  .notes.add_chord(root=F4, quality=major, beat=4, length=4)
  .notes.add_chord(root=G4, quality=major, beat=8, length=4)
  .notes.add_chord(root=A3, quality=minor, beat=12, length=4)
```

### Arpeggiated lead with swing

```
track(name="Lead", new=true)
  .clip.new(bar=1, length_bars=4)
  .notes.add_arpeggio(root=C4, quality=major, step=0.25, pattern=updown, fill=true)
track(name="Lead").fx.add(name="reverb").track.set(volume_db=-6)
groove.set(template="Basic 8th Swing", strength=0.6)
```

### Split arpeggios across a clip

```
track(name="Arp", new=true)
  .clip.new(length_bars=4)
  .notes.add_arpeggio(root=E4, quality=min, beat=0, step=0.5, beats=8)
  .notes.add_arpeggio(root=C4, quality=major, beat=8, step=0.5, beats=8)
```

### Bulk operations

```
// Mute all drum tracks
filter(tracks, track.name == "Drums").track.set(mute=true)

// Add compressor to every track named "Vocals"
filter(tracks, track.name == "Vocals").for_each(.fx.add(name="compressor"))

// Delete all tracks named "Scratch"
filter(tracks, track.name == "Scratch").delete()
```

### Custom groove from scratch

```
groove.new(name="Lazy Swing", notesPerBeat=2, shifts="0.0,0.33")
groove.set(template="Lazy Swing", strength=0.8)
```
