# Sampler

The Sampler is MAGDA's built-in sample player. It plays one sample chromatically across the keyboard, or a map of zones spread over keys and velocities. Every voice has its own pitch envelope, filter, and amplitude envelope. It also imports SFZ, DecentSampler, and SoundFont (SF2/SF3) instruments.

## Overview

The Sampler has 8 voices. Every zone a note falls into plays on its own voice, so a note that answers two layered zones takes two. When all voices are busy, a playing voice is stolen for the new note.

The faceplate has four tabs, **Sample**, **Pitch**, **Filter**, and **Amp**, each a plot with its controls beside it. The **Show the zones** toggle in the device's sidebar opens a left panel with two tabs: **ZONES**, the zone list, and **VOICE**.

## Loading samples

- Drag an audio file from the Media Explorer or your file manager onto the Sampler.
- Or click the empty waveform area, which shows a **+** and *Drop a sample or click to load*, to open a file chooser. It accepts WAV, AIFF, FLAC, Ogg, and MP3 files, and the instrument formats described under [Importing instruments](#importing-instruments).

Once a sample is loaded, a strip over the waveform shows its name, its **ROOT** note, and a folder button that loads another. The root note comes from the file's metadata when it has one, and is C3 (note 60) otherwise. Drag or type in the root box to change it; it reads note names such as `C3`, `F#2`, or `Bb1`.

Dropping one sample on the device replaces the current one. Dropping several files, or a folder, builds a zone map from them (see [Mapping by name](#mapping-by-name)). Choosing several files in the file chooser does the same.

## Sample tab

The waveform draws the way an audio clip does, one lane per channel. A ruler runs along the top in seconds; right-click it to switch to samples. Use the scroll bar underneath to zoom and scroll.

The **START**, **END**, **LOOP IN**, and **LOOP OUT** boxes beside the waveform set the playback region in frames. Type a value, or click the arrows to step one frame at a time. You can also drag the markers on the waveform.

The **loop** switch beside the tabs turns looping on. While it is on, it lights, a held note repeats the stretch between the loop markers, and the waveform shows the loop region and its markers. While it is off, the waveform shows the start and end markers, and **LOOP IN** and **LOOP OUT** are greyed out and locked.

### Expanding the waveform

The expand button beside the loop switch (*Expand into the arrangement*) opens the waveform on its own across the arrangement area, with its ruler, scroll bar, and markers. While it is out, its place on the device reads *The waveform is in the arrangement*, and the Pitch, Filter, and Amp tabs stay usable. Click the collapse button in the expanded view, or press ++esc++, to put it back.

The expand button in the device header opens the [zone map](#zone-map) over the waveform instead. Only one is ever out: clicking the other button swaps them.

## Pitch tab

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Pitch** | -24 to 24 st | 0 | Transpose in semitones |
| **Fine** | -100 to 100 cents | 0 | Detune in cents |
| **Env** | -48 to 48 st | 0 | Pitch envelope depth |
| **Attack** | 1 ms to 5 s | 1 ms | Pitch envelope attack |
| **Decay** | 1 ms to 5 s | 200 ms | Pitch envelope decay |
| **Sustain** | 0 to 100% | 0% | Pitch envelope sustain level |
| **Release** | 1 ms to 10 s | 100 ms | Pitch envelope release |

The pitch envelope bends each note by up to **Env** semitones, for drum thumps, risers, and plucks from a single sample. A negative depth bends the note down, and the plot hangs the shape from the top. At 0 the envelope does nothing. Drag the handles on the plot to shape it.

## Filter tab

Each voice runs its own filter. Pick the engine from the dropdown in the tab's heading: **Off**, **SVF**, **Ladder**, **Korg 35**, **Oberheim**, **Sallen-Key**, **Diode**, or **Comb**. It starts **Off**, and the other controls dim until you choose one.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Mode** | Depends on the engine | LP | Filter response, picked from icons |
| **Poles** | 2 / 4 / 6 | 2 | SVF steepness |
| **Cutoff** | 20 Hz to 20 kHz | 1 kHz | Filter frequency |
| **Res** | 0 to 100% | 0% | Resonance |
| **Key** | 0 to 100% | 0% | How far the cutoff follows the played note |
| **Vel** | 0 to 100% | 0% | How far velocity opens the cutoff |
| **Env** | -72 to 72 st | 0 | Filter envelope depth |
| **Attack** | 1 ms to 5 s | 1 ms | Filter envelope attack |
| **Decay** | 1 ms to 5 s | 300 ms | Filter envelope decay |
| **Sustain** | 0 to 100% | 0% | Filter envelope sustain level |
| **Release** | 1 ms to 10 s | 100 ms | Filter envelope release |

**Mode** is a row of icons for the responses the chosen engine has; hover one for its name. The SVF has **LP**, **BP**, **HP**, **Notch**, and **Notch+**, a notch at the cutoff between two resonant peaks half an octave either side. **Comb** has **Comb+** and **Comb-**. **Poles** applies to the SVF's LP, BP, HP, and Notch: more poles cut more steeply, with the resonance on the last stage.

## Amp tab

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 1 ms to 5 s | 1 ms | Time to reach full level after a note-on |
| **Decay** | 1 ms to 5 s | 100 ms | Time to fall from peak to sustain level |
| **Sustain** | 0 to 100% | 100% | Level held while the note is held |
| **Release** | 1 ms to 10 s | 100 ms | Time to fade out after note-off |
| **Level** | -60 to 12 dB | 0 | Output level |
| **Velocity** | 0 to 100% | 100% | Velocity to amplitude depth |

## Voice tab

The **VOICE** tab in the left panel holds two controls:

- **Voice Mode** - **Poly** plays a voice per note. **Mono** plays one note at a time and retriggers on each new note. **Legato** also plays one note at a time, but overlapping notes slide to the new pitch without retriggering. In Mono and Legato, a note plays the first zone that answers it.
- **Glide** - portamento time in Mono and Legato, 0 to 2000 ms.

## Zones

A zone is a sample with its own root note, key and velocity ranges with fades, tune, gain, start and end markers, loop switch, and loop points, and a round-robin switch. Every zone a note's key and velocity fall into plays, scaled by its fades.

With a single sample, the Sampler is one zone whose markers are device parameters: you can automate **START**, **END**, **LOOP IN**, and **LOOP OUT** and link them to macros and modulators. Once there is a zone list, each zone keeps its own markers, root, and loop, and the faceplate edits the selected zone. Editing the list back down to one zone returns it to the single-sample form.

### Zone list

The **ZONES** tab lists the zones the way a rack lists its chains. Each row shows the zone's file name, or *Zone N* while it has no sample. Click a row to select it; the strip over the waveform then reads the file name with *zone N of M*.

The list has three views:

| View | What each row shows |
|------|---------------------|
| **MIX** | A gain slider (-48 to 12 dB) and a button to remove the zone |
| **KEY** | The key range as a bar, with its fades on the shoulders |
| **VEL** | The velocity range as a bar, and the **RR** switch |

On a range bar, drag an end to set the range, the middle to move it, or a top corner to fade in or out. Double-click for the whole range.

**Add zone** at the foot of the list adds an empty zone. Drop a sample on a zone's row to fill it; the zone keeps its ranges, tune, and gain. Dropping samples elsewhere on the list adds each as a new zone. With a zone list, loading a single sample, by the folder button or by dropping it on the waveform, fills the selected zone instead of replacing the map.

### Round robin

Zones with **RR** on take turns. Of the RR zones a note falls into, only one plays, and the next note plays the next one. Zones with **RR** off always play. Use it to alternate between several takes of the same hit.

### Mapping by name

When you drop several samples or a folder, the Sampler reads a note from each file name: a note name such as `C3`, `F#2`, or `Db-1` (C3 is note 60), or a number from 0 to 127. A note name wins over a number. A file with neither uses the root note in its metadata; anything left over is placed one key at a time above the highest mapped note, or from C3 when nothing is.

Each root's key range reaches halfway to its neighbours, and the lowest and highest zones extend to the ends of the keyboard. Several files on the same note become velocity layers that split the velocity range evenly, in file-name order.

### Zone map

The device header's expand button opens the zone map over the selected zone's waveform. Along the top are the selected zone's **Root**, **Low key**, **High key**, **Low vel**, **High vel**, **Tune** (-1200 to 1200 cents), and **Gain** (-48 to 12 dB).

Below them is a grid of key across and velocity up, with each zone drawn as a box and its root as a tick on the box's floor. Drag a box's edges to change its ranges, or its middle to move it; the root moves with it. Right-click a zone for **Remove zone**, or select it and press ++backspace++ or ++delete++.

Drop one sample on the grid to add a zone on the key under the pointer, covering that key alone. Drop several, or a folder, to map them by name.

## Importing instruments

Drop or load an `.sfz`, `.dspreset` (DecentSampler), `.sf2`, or `.sf3` file to replace the Sampler's zones with the instrument's. The instrument's envelopes and filter go onto the device's own controls. A toast says how many zones came in and lists what the Sampler can't play under *Not imported*.

Envelope, filter, and velocity settings the file doesn't set go back to their defaults, so the previous sound's filter doesn't carry over. **Level**, **Pitch**, **Fine**, **Voice Mode**, and **Glide** keep their values.

### SFZ

Settings inherit through `<control>`, `<global>`, `<master>`, `<group>`, and `<region>`, and `#define` and `#include` work. Each region becomes a zone with:

- key and velocity ranges, with crossfades
- root note (`pitch_keycenter`), tune, and transpose. SFZ calls note 60 C4, where MAGDA calls it C3.
- volume and amplitude, including the global, master, and group volumes
- offset and end, and loop mode. When the SFZ sets no loop, a loop stored in the WAV file plays.
- round robin from `seq_length`

The amplitude, pitch, and filter envelopes, and the filter type, cutoff, resonance, key tracking, and velocity tracking, are device-wide on the Sampler, so they come from the first region. Regions that set them differently are listed in the toast.

Layers switched by MIDI CC, MIDI program, or keyswitch keep only what plays at load time: each CC at its `set_cc` value, program 0, and the default keyswitch. Other features the Sampler can't play are listed in the toast rather than imported, among them LFOs, CC modulation, choke groups, velocity curves, release triggers, and random round robin (which plays in turn).

### DecentSampler

The same settings are read from a `.dspreset`'s groups and samples. Low-pass, band-pass, high-pass, and notch filter effects map onto the Sampler's filter; other effects are listed as not imported.

### SF2 and SF3

A SoundFont holds many presets, so a menu asks which one to import, grouped by bank when there is more than one bank. The chosen preset's samples are written as WAV files to a `SoundFonts` folder in MAGDA's data folder (set in **Settings > Paths**), and the zones play those files. SF3 samples are decoded, and linked stereo pairs become one stereo file.

Attenuation is scaled by 0.4, as FluidSynth and E-mu hardware do, which is how General MIDI banks are voiced. The envelopes and filter come from the zone that plays note 60.

## Macros, modulation, and presets

The Sampler sits in the standard device slot, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and its settings save and recall through the [preset system](../panels/browsers.md).
