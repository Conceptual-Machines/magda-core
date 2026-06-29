# Poly Synth

Poly Synth is a 16-voice subtractive synthesizer built into MAGDA. Four detunable oscillators feed a multimode state-variable filter with its own envelope, followed by an amplitude ADSR. It plays in poly, mono, or legato modes.

Poly Synth is a separate device from the [4OSC Synth](4osc.md). 4OSC is the Tracktion Engine synth; Poly Synth is a native MAGDA instrument compiled from Faust.

## Oscillators

Four oscillators, each with the same four controls. Oscillators 2 to 4 default to silent (level -60 dB), so a fresh patch sounds from Osc 1 alone until you bring the others up.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Wave** | Sine / Saw / Square / Triangle | Saw | Oscillator waveform |
| **Level** | -60 to 6 dB | -12 (Osc 1) | Oscillator gain |
| **Coarse** | -24 to 24 st | 0 | Pitch in semitones |
| **Fine** | -100 to 100 cents | 0 | Pitch in cents |

Each oscillator also has an **Enable** toggle (on by default) and a **Reset** toggle that resets its phase on every note-on for a consistent attack.

## Filter

A multimode state-variable filter with a dedicated envelope.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Type** | LP / HP / BP / Notch | LP | Filter topology |
| **Cutoff** | 50 to 18000 Hz (log) | 3000 | Filter frequency |
| **Resonance** | 0 to 0.95 | 0.3 | Filter Q |
| **Slope** | 12 dB / 24 dB | 12 dB | Filter steepness |
| **Drive** | 0 to 1 | 0 | Pre-filter saturation |
| **Filter Env** | -4 to 4 oct | 0 | Envelope depth on cutoff (bipolar) |

The filter envelope has its own **Attack** (1 to 2000 ms), **Decay** (1 to 2000 ms), **Sustain** (0 to 1), and **Release** (1 to 4000 ms).

## Amp

The amplitude envelope shapes the level of each note.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 1 to 2000 ms | 5 | Envelope attack |
| **Decay** | 1 to 2000 ms | 200 | Envelope decay |
| **Sustain** | 0 to 1 | 0.7 | Sustain level |
| **Release** | 1 to 4000 ms | 400 | Envelope release |

## Voice and output

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Voice Mode** | Poly / Mono / Legato | Poly | Voicing |
| **Glide** | 0 to 2000 ms | 0 | Portamento time (mono and legato only) |
| **Bend Range** | 0 to 24 st | 2 | Pitch-wheel range |
| **Vel > Amp** | 0 to 1 | 1 | Velocity to amplitude depth |
| **Vel > Filter** | 0 to 6 oct | 0 | Velocity to filter cutoff depth |
| **Output** | -60 to 6 dB | 0 | Master output level |

## Macros, modulation, and presets

Like every MAGDA instrument, Poly Synth lives in the standard device slot, so its parameters can be driven by [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).
