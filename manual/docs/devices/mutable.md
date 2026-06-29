# Mutable Instruments Ports

MAGDA ships native ports of three Mutable Instruments modules, faithful to Emilie Gillet's open-source designs:

- **Materia** is a port of **Elements**
- **Halo** is a port of **Rings**
- **Nimbus** is a port of **Clouds**

Materia and Halo are instruments played from MIDI. Nimbus is a granular audio processor that works on an existing signal, so add it to a track's FX chain rather than playing it from the keyboard.

## Materia (Elements)

A monophonic modal-synthesis voice with three exciters, **Bow**, **Blow**, and **Strike**, driving a modal and string resonator, followed by a stereo **Space** reverb.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Contour** | 0 to 100% | 50 | Exciter envelope shape |
| **Bow** | 0 to 100% | 0 | Bow exciter amount |
| **Bow Timbre** | 0 to 100% | 50 | Bow timbre |
| **Blow** | 0 to 100% | 0 | Blow exciter amount |
| **Blow Flow** | 0 to 100% | 50 | Blow pressure and flow |
| **Blow Timbre** | 0 to 100% | 50 | Blow brightness |
| **Strike** | 0 to 100% | 80 | Strike exciter amount |
| **Strike Mallet** | 0 to 100% | 50 | Mallet hardness |
| **Strike Timbre** | 0 to 100% | 50 | Strike spectral content |
| **Signature** | 0 to 100% | 10 | Resonator timbral colour |
| **Geometry** | 0 to 100% | 30 | Resonator shape (bar, membrane, tube) |
| **Brightness** | 0 to 100% | 50 | Resonator cutoff |
| **Damping** | 0 to 100% | 70 | Mode decay rate |
| **Position** | 0 to 100% | 30 | Strike position on the resonator |
| **Space** | 0 to 100% | 30 | Stereo reverb amount |
| **Pitch** / **Fine** | -24 to 24 st / -100 to 100 cents | 0 | Transpose and fine tune |
| **Level** | -60 to 12 dB | 0 | Output level |
| **Vel > Amp** | 0 to 100% | 100 | Velocity to amplitude depth |

## Halo (Rings)

A switchable polyphonic resonator with an internal exciter. There is no note-off: notes ring out and decay according to **Damping**, and the resonator rotates its internal voices as you play.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Structure** | 0 to 100% | 25 | Modal-to-string character |
| **Brightness** | 0 to 100% | 50 | Resonator cutoff |
| **Damping** | 0 to 100% | 50 | Mode decay rate |
| **Position** | 0 to 100% | 25 | Excitation position |
| **Model** | Modal / Sympathetic / String / FM / Sym Quant / String+Verb | Modal | Resonator type |
| **Polyphony** | 1 / 2 / 4 voices | 2 | Internal voice count |
| **Chord** | 0 to 10 | 0 | Chord voicing (modal and chord models) |
| **Pitch** / **Fine** | -24 to 24 st / -100 to 100 cents | 0 | Transpose and fine tune |
| **Level** | -60 to 12 dB | 0 | Output level |

## Nimbus (Clouds)

A stereo granular texture processor with four modes. It processes whatever audio reaches it, with an internal diffuser and a freeze control. Add it to a track's [FX chain](../fx-chain.md).

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Position** | 0 to 100% | 50 | Playback position / seek |
| **Size** | 0 to 100% | 50 | Grain size and buffer length |
| **Pitch** | -24 to 24 st | 0 | Pitch shift (Stretch mode) |
| **Density** | 0 to 100% | 40 | Grain density and overlap |
| **Texture** | 0 to 100% | 50 | Grain envelope and spectral tilt |
| **Dry/Wet** | 0 to 100% | 50 | Processed-to-dry blend |
| **Spread** | 0 to 100% | 50 | Stereo width and grain randomisation |
| **Feedback** | 0 to 100% | 0 | Output to input feedback |
| **Reverb** | 0 to 100% | 0 | Diffuser tail amount |
| **Mode** | Granular / Stretch / Looping Delay / Spectral | Granular | Processing algorithm |
| **Freeze** | Off / On | Off | Freeze the buffer and loop the current grains |

## Macros, modulation, and presets

All three ports are standard MAGDA devices, so their parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).
