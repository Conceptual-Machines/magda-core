# Built-in Devices

MAGDA ships with its own native MIDI devices, instruments, and effects. All built-in devices are managed through the same rack wrapper layer that hosts third-party plugins, so macros, modulators, and presets behave identically across the device set.

## MIDI Devices

| Device | Description |
|--------|-------------|
| **Arpeggiator** | MIDI arpeggiator with pattern, swing, and time bend. See [Arpeggiator](arpeggiator.md). |
| **Chord Engine** | Real-time chord detection, suggestion, and AI progression generator. See [Chord Engine](chord-engine.md). |
| **Step Sequencer** | Programmable step sequencer with AI pattern generation. See [Step Sequencer](step-sequencer.md). |

## Instruments

| Device | Description |
|--------|-------------|
| **4OSC Synth** | Four-oscillator subtractive synthesizer with internal mod matrix. See [4OSC Synth](4osc.md). |
| **Drum Grid** | Chain-based drum machine with per-pad FX. See [Drum Grid](drum-grid.md). |
| **Sampler** | 8-voice sample player with ADSR and pitch controls. See [Sampler](sampler.md). |

## Effects

The MAGDA FX bank is a set of native effects compiled from Faust DSP and shipped with every MAGDA installation. See [Effects](effects.md) for the full list, grouped by category.

## External Plugins

In addition to built-in devices, MAGDA supports external plugins in the following formats:

- **VST3** - Cross-platform
- **Audio Units (AU)** - macOS only

Use the [Plugin Browser](../panels/browsers.md) to find and add plugins to your tracks.
