# Effects (MAGDA FX Bank)

The MAGDA FX bank is a set of native effects compiled from Faust DSP and shipped with every MAGDA installation. They appear in the Plugin Browser under the **MAGDA** vendor and can be dragged onto any track's FX chain.

## Dynamics

| Device | Description |
|--------|-------------|
| **Compressor** | Two engines. **Clean**: feedforward with peak/RMS detection, soft knee, stereo link, sidechain HPF, external audio sidechain, parallel mix, output safety limiting. **Glue**: Brouns FBFF compressor with Detector (Peak/RMS), Style (Pre/Post), and FBFF blend. |
| **Multiband Compressor** | OTT-style 3-band compressor. LR4 crossover split, two OTT stages in series per band, symmetric expander, per-band brickwall limiter, editable crossover frequencies and per-band thresholds in the curve editor. |
| **Limiter** | Sanfilippo lookahead brickwall limiter. 5 ms lookahead, peak-holder, tau-smoothed attack / release. Threshold sets the output ceiling in dB. |
| **Clipper** | Antialiased multi-mode clipper with five ADAA curves: Hard (brickwall), Soft (quadratic), Tanh (tube-style), Hyperbolic, and Sine. Drive pushes input into the curve, Output trims after. |
| **Gate** | Stereo-linked gate / downward expander with range, timing (attack / hold / release), mix, and output gain. |

## EQ & Filter

| Device | Description |
|--------|-------------|
| **EQ** | 8-band parametric EQ. Each band selectable between HP, Low Shelf, Bell, High Shelf, LP, and Notch. |
| **Filter** | Six-model multimode filter. **SVF**: clean 2-pole LP/BP/HP/Notch. **Ladder**: 4-pole low-pass with driven resonance. **Korg 35**: MS-style LP/HP with analog bite. **Oberheim**: SEM-style LP/BP/HP/Notch. **Sallen-Key**: smooth 2nd-order LP/BP/HP. **Diode**: resonant 4-pole diode ladder with input drive. |

## Reverb & Space

| Device | Description |
|--------|-------------|
| **Reverb** | Three engines. **Plate**: Dattorro diffusion network. **Hall**: Zita 8-tap FDN. **Room**: Freeverb Schroeder/Moorer network. |
| **Dimension** | Stereo widener with three engines. **Dimension**: Roland Dimension D-style anti-phase modulated delays. **Haas**: short fixed delay on one channel. **M/S**: pure side-channel gain, no time smear. |

## Delay

| Device | Description |
|--------|-------------|
| **Delay** | Stereo delay with sync (or free time), tone, feedback, and crossfeed. |
| **Grain Delay** | Granular delay for smeared repeats, pitch motion, and texture. |

## Modulation

| Device | Description |
|--------|-------------|
| **Chorus** | Stereo chorus with 1-3 modulated voices per channel. Free-Hz or tempo-synced rate, depth, feedback, mix, stereo width. |
| **Flanger** | Stereo flanger with short modulated delay, heavy feedback for the classic comb-sweep, and sync- or free-rate LFO. |
| **Phaser** | Phaser with selectable stage count, feedback, and sweep window. |
| **Mod** | Tremolo / vibrato / auto-pan sharing one LFO. Free-Hz or tempo-synced; sine, triangle, square, or sample-and-hold shape. |
| **Ring Mod** | Stereo ring modulator. Sine, triangle, or square carrier from 1 Hz (tremolo) to 5 kHz (metallic clang). Sync- or free-rate. |
| **Freq Shift** | Stereo single-sideband frequency shifter. Hilbert-pair Bode design. Fixed-Hz offset, feedback for resonant artefacts, Spread for stereo width. |

## Distortion

| Device | Description |
|--------|-------------|
| **Saturator** | Waveshaper with drive, mode, bias, tone, mix, and output. |
| **Grit** | Bit-depth and sample-rate reduction. |
| **Bitcrusher** | Lo-fi bitcrusher. Rate (sample-rate reduction), Bits (bit depth), Drive (quantization landing point), Tone (post-crush low-pass). |

## Pitch

| Device | Description |
|--------|-------------|
| **Pitch** | Three engines, all using `ef.transpose`. **Shifter**: single voice, +/-24 semitones. **Detuner**: two voices hard-panned L/R for chorus-style thickening. **Harmonizer**: shifted voice summed with dry at a chosen interval. |

## Utility

| Device | Description |
|--------|-------------|
| **Utility** | Gain, pan, stereo width, mono sum, low-frequency mono, and per-channel polarity flip. |

!!! note
    Devices that bundle multiple engines (Compressor, Reverb, Dimension, Filter, Pitch) expose an engine selector at the top of the editor and switch DSP in place. Macros and modulator links survive the switch.

## Faust (Custom DSP)

The **Faust** device hosts a [Faust](https://faust.grame.fr) DSP that you compile and load at runtime. Unlike the rest of the MAGDA FX bank, where each device wraps a fixed pre-compiled `.dsp` source, this device accepts any Faust program.

- **Folder icon** - load a `.dsp` file from disk. The source is compiled by the libfaust interpreter and swapped in immediately.
- **Script icon** - opens an in-app code editor for live editing. Saving recompiles and hot-swaps the DSP.
- The current script name is shown in the header banner ("Drive" in the example).
- Arrows in the header step through parameter pages when the DSP exposes more controls than fit on one screen.

Parameters live in a stable pool of slots that persist across recompiles, so macro links, modulator routings, MIDI Learn assignments, and automation lanes survive a code change as long as the slot ordering is preserved. This makes the device practical for iterative DSP development without losing your patch state on every save.

!!! warning "Prototype"
    This device is still a prototype. The DSP is run through the **libfaust interpreter** rather than ahead-of-time-compiled native code, so CPU usage is significantly higher than the pre-compiled MAGDA FX bank. Use it for sketching and experimentation; for production work, ask for the patch to be promoted into a compiled device.
