# Built-in Devices

MAGDA ships with its own native MIDI devices, instruments, and effects. All built-in devices are managed through the same rack wrapper layer that hosts third-party plugins, so macros, modulators, and presets behave identically across the device set.

## MIDI Devices

| Device | Description |
|--------|-------------|
| **Arpeggiator** | MIDI arpeggiator with pattern, swing, and time bend. See [Arpeggiator](arpeggiator.md). |
| **Chord Engine** | Real-time chord detection, suggestion, and AI progression generator. See [Chord Engine](chord-engine.md). |
| **Step Sequencer** | Monophonic step sequencer, up to 32 steps, with time bend and AI pattern generation. See [Step Sequencer](step-sequencer.md). |
| **Poly Sequencer** | Polyphonic step sequencer with up to eight notes per step and a keys or drum-lane grid. See [Poly Sequencer](poly-sequencer.md). |
| **Strum** | Turns a held chord into a strum, roll or arpeggio shaped by a curve, for any instrument. See [Strum](strum.md). |

## Instruments

| Device | Description |
|--------|-------------|
| **Poly Synth** | 16-voice subtractive synth: four oscillators with unison, and two filters in series or parallel. See [Poly Synth](poly-synth.md). |
| **FM0** | Four-operator FM synth with a modulation matrix and a filter that can sit inside it. See [FM0](fm0.md). |
| **Drum Kit** | Five synthetic drum voices: Kick, Snare, Hat, Tom, Clap. See [Drum Kit](drum-kit.md). |
| **Physical Models** | Struck Marimba, Djembe, and Bell physical models. See [Physical Models](physical-models.md). |
| **Materia / Halo / Nimbus** | Ports of Mutable Instruments Elements, Rings, and Clouds. See [Mutable Ports](mutable.md). |
| **Drum Grid** | Pad-based drum instrument: each pad has its own device chain and its own mixer channel. See [Drum Grid](drum-grid.md). |
| **Sampler** | 8-voice sample player with zones, a pitch envelope and a filter per voice; imports SFZ, DecentSampler and SoundFont instruments. See [Sampler](sampler.md). |
| **Faust Instrument** | 16-voice synth that plays a Faust DSP you write and compile at runtime. See [Faust Instrument](faust-instrument.md). |

## Effects

The MAGDA FX bank is a set of native effects compiled from Faust DSP and shipped with every MAGDA installation. See [Effects](effects.md) for the full list, grouped by category. The bank is joined by native devices that are not compiled from Faust, among them **[Convolution](effects.md#convolution)**, a convolution reverb that plays the signal through an impulse response you load, **[Spectral FX](effects.md#spectral-fx)**, four FFT effects over a scrolling spectrogram, and **[Sidechain](effects.md#sidechain)**, a MIDI-triggered volume shaper keyed from another track.

## Analysis

Real-time analyzers — an **Oscilloscope**, a **Spectrum Analyzer** and a **Levels** loudness meter — that tap the signal without changing it, on the FX chain or in the post-FX area. See [Analysis](analysis.md).

## External Plugins

In addition to built-in devices, MAGDA supports external plugins in the following formats:

- **VST3** - Cross-platform
- **Audio Units (AU)** - macOS only

Use the [Plugin Browser](../panels/browsers.md) to find and add plugins to your tracks.
