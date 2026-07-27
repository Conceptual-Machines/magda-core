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

## Project

Project-wide timing settings.

```
project.set(bpm=128)
project.set(time_signature="7/8")
project.set(bpm=128, time_signature="3/4")
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `bpm` | number | Project tempo. `tempo` is accepted as a synonym. |
| `time_signature` | string | Written as `numerator/denominator`, e.g. `"7/8"` |

At least one of the two must be given.

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
| `colour` | string | Track colour as a hex string, e.g. `"#ff5a36"` |
| `volume_db` | number | Volume in dB |
| `pan` | number | Pan position (-1.0 left to 1.0 right) |
| `mute` | bool | Mute the track |
| `solo` | bool | Solo the track |

### Grouping and ordering

```
track(id=1).track.group(name="Drums", tracks="1,2,3")   // Group tracks by 1-based id
track(id=4).track.move(index=1)                          // Move to first position
```

`track.group` switches the context to the new group, so you can keep chaining onto it. `track.move` positions the track among its siblings — top-level tracks, or the children of its parent group.

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

### Enabling and disabling clips

```
track(name="Bass").clip.set(enabled=false, index=0)     // Disable clip at index
track(name="Bass").clip.set(enabled=false)              // Disable the selected clips
```

`clip.set` currently takes `enabled` only. Without `index` it acts on the current selection, which makes it natural to chain after a `clips.select(...)` in the same statement. See [Enabling and Disabling Clips](../clips.md#enabling-and-disabling-clips).

### Selecting clips

```
track(id=1).clips.select(clip.length_bars >= 2)
track(id=1).clips.select(clip.start_bar == 1)
track(id=1).clips.select(clip.name == "Intro")
track(id=1).clips.select()                              // Every clip on the track
```

| Field | Type | Description |
|-------|------|-------------|
| `clip.length_bars` | number | Clip length in bars |
| `clip.start_bar` | number | Clip start position, in bars, 1-based |
| `clip.start_beats` | number | Clip start position in beats |
| `clip.length` | number | Clip length in seconds |
| `clip.start` | number | Clip start position in seconds |
| `clip.id` | number | Clip id |
| `clip.track_id` | number | Owning track id |
| `clip.name` | string | Clip name |
| `clip.type` | string | Clip type |

Prefer the bar and beat fields. `clip.length` and `clip.start` are seconds derived from the current tempo, so a predicate written against them stops matching if the tempo changes.

Operators: `==`, `!=`, `>`, `>=`, `<`, `<=`. String fields accept `==` and `!=` only. Omitting the condition entirely selects every clip on the track.

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
| `name` | string | Device name or alias |
| `format` | string | Optional: `VST3`, `AU`, or `VST` to disambiguate |

**MAGDA devices** are added by exact display name or by a canonical alias — `eq`, `compressor`, `reverb`, `delay`, `chorus`, `phaser`, `filter`, `utility`, `pitch shift`, `ir reverb` and the rest of the [device registry](../devices/built-in.md). The alias list is generated from the registry itself, so it follows whatever is installed rather than a fixed set.

Third-party plugins are referenced by their scanned name, or by an alias token such as `<pro_q_3>` or `<surge_xt>`. Use the `@alias` syntax in the AI chat to autocomplete plugin names.

## Racks

Rack operations act on the track in context. Each one remembers the rack (and chain) it just touched, so a following call can omit the ids.

```
track(id=1).rack.new(name="Parallel").rack.chain_new(name="Wet")
track(id=1).rack.set(id=12, bypassed=true, volume_db=-3)
track(id=1).rack.chain_set(rack_id=12, chain_id=34, volume_db=-6, pan=0.2)
track(id=1).rack.chain_delete(rack_id=12, chain_id=34)
track(id=1).rack.delete(id=12)
```

| Method | Parameters |
|--------|------------|
| `rack.new` | `name` (default `"Rack"`). Creates the rack with its default chain. |
| `rack.delete` | `id` — defaults to the rack just created |
| `rack.set` | `id`, plus `bypassed` and/or `volume_db` |
| `rack.chain_new` | `rack_id` (defaults to the current rack), `name` (default `"Chain"`) |
| `rack.chain_delete` | `rack_id`, `chain_id` |
| `rack.chain_set` | `rack_id`, `chain_id`, plus any of `name`, `muted`, `solo`, `bypassed`, `volume_db`, `pan`, `output` |

`rack.set` and `rack.chain_set` need at least one property to change, and report an error otherwise. Rack and chain ids come from the state snapshot — see [FX Chain & Racks](../fx-chain.md).

While a rack chain is in scope, `fx.add` puts the device **inside that chain** rather than on the track's top-level chain. That is what makes a single statement able to build a rack and fill it.

## Filter Operations

Bulk operations on tracks matching a condition.

```
filter(tracks, track.name == "Drums").track.set(mute=true)
filter(tracks, track.name == "Drums").delete()
filter(tracks, track.name == "Drums").select()
filter(tracks, track.name == "Drums").track.group(name="Group")
filter(tracks, track.name == "Drums").for_each(.clip.new(bar=1, length_bars=4))
filter(tracks, track.name == "Drums").for_each(.fx.add(name="reverb").track.set(volume_db=-6))
```

The only supported predicate is an exact name match, `track.name == "..."`. Omitting the condition entirely targets **every** track:

```
filter(tracks).track.set(mute=true)
filter(tracks).track.group(name="All Tracks")
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

## Built-in Functions

```
track(id=1).clip.new(length_bars=random(1, 4))
```

`random(min, max)` returns a value between `min` and `max` inclusive — an integer when both arguments are integers, a float otherwise.

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

### Parallel rack

```
track(name="Drums")
  .rack.new(name="Parallel")
  .rack.chain_new(name="Crushed")
  .fx.add(name="compressor")
```

### Set the session up in 3/4 at 96 BPM

```
project.set(bpm=96, time_signature="3/4")
```

### Disable every clip shorter than a bar

```
track(id=1).clips.select(clip.length_bars < 1).clip.set(enabled=false)
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
