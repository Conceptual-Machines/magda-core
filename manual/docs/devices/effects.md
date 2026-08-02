# Effects (MAGDA FX Bank)

The MAGDA FX bank is a set of native effects compiled from Faust DSP and shipped with every MAGDA installation. They appear in the Plugin Browser under the **MAGDA** vendor and can be dragged onto any track's FX chain.

## Dynamics

| Device | Description |
|--------|-------------|
| **Compressor** | Two engines. **Clean**: feedforward with peak/RMS detection, soft knee, stereo link, sidechain HPF, external audio sidechain, parallel mix, output safety limiting. **Glue**: Brouns FBFF compressor with Detector (Peak/RMS), Style (Pre/Post), and FBFF blend. |
| **Multiband Compressor** | OTT-style 3-band compressor. LR4 crossover split, two OTT stages in series per band, symmetric expander, per-band brickwall limiter, editable crossover frequencies and per-band thresholds in the curve editor. |
| **Limiter** | Native lookahead limiter / autonormalizer. Threshold drives the signal into a fixed 0 dB ceiling, Attack and Release shape gain movement, and Output is a post-limiter trim that can only reduce level. |
| **Clipper** | Antialiased multi-mode clipper with five ADAA curves: Hard (brickwall), Soft (quadratic), Tanh (tube-style), Hyperbolic, and Sine. Drive pushes input into the curve, Output trims after. |
| **Gate** | Stereo-linked gate / downward expander with range, timing (attack / hold / release), mix, and output gain. |

## EQ & Filter

| Device | Description |
|--------|-------------|
| **EQ** | 8-band parametric EQ. Each band selectable between HP, Low Shelf, Bell, High Shelf, LP, and Notch. The curve view draws the live signal spectrum behind the response curve — a dim trace for the input and a brighter one for the output — so you can see what each band is doing to the actual material. |
| **Filter** | Six-model multimode filter. **SVF**: clean 2-pole LP/BP/HP/Notch. **Ladder**: 4-pole low-pass with driven resonance. **Korg 35**: MS-style LP/HP with analog bite. **Oberheim**: SEM-style LP/BP/HP/Notch. **Sallen-Key**: smooth 2nd-order LP/BP/HP. **Diode**: resonant 4-pole diode ladder with input drive. |

## Reverb & Space

| Device | Description |
|--------|-------------|
| **Reverb** | Three engines. **Plate**: Dattorro diffusion network. **Hall**: Zita 8-tap FDN. **Room**: Freeverb Schroeder/Moorer network. |
| **Dimension** | Stereo widener with three engines. **Dimension**: Roland Dimension D-style anti-phase modulated delays. **Haas**: short fixed delay on one channel. **M/S**: pure side-channel gain, no time smear. |
| **IR Reverb** | Convolution reverb that plays the signal through an impulse response you load. See [IR Reverb](#ir-reverb) below. |

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

## IR Reverb

The **IR Reverb** is a native MAGDA device (not part of the Faust FX bank) that sits under **Reverb** in the Plugin Browser. Where the Reverb device synthesises a space from an algorithm, this one convolves your signal with a recording of a real one: an impulse response captured in a hall, a stairwell, a plate, a spring tank, a guitar cabinet, or any object you have measured. Anything an impulse response can describe, it will reproduce, which also makes it the device to reach for when you want a specific room rather than a plausible one.

No impulse responses ship with MAGDA, so the device starts empty and passes the signal through until you load one.

**Loading an impulse response.** Either click the **folder button** at the top right of the faceplate and pick a file, or drag an audio file straight onto the device. WAV, AIFF, FLAC and Ogg are all accepted, mono or stereo. The loaded file's name replaces the *No IR loaded* label in the header.

The impulse response is stored inside the device, not referenced from disk, so a project stays self-contained: it opens with its reverb intact on a machine that has never seen the original file. Long impulse responses make for large project files.

**Controls:**

| Control | Description |
|---|---|
| **LOW CUT** | High-pass on the reverb, 10 Hz to 20 kHz. Clears mud out of the tail. |
| **HIGH CUT** | Low-pass on the reverb, 10 Hz to 20 kHz. Darkens the tail so it sits behind the dry signal. |
| **Q** | Resonance of both cutoff filters, 0.1 to 14, shared between them. |
| **GAIN** | Output trim, -12 to +6 dB. |
| **MIX** | Dry/wet balance, 0 to 100%. At 0% the device is a straight passthrough. |

!!! note
    The device reports no latency, so it needs no delay compensation and can be used freely on a live-monitored track. A long impulse response is still expensive to convolve, though: if the CPU meter climbs, a shorter one costs less.

## Sidechain

The **Sidechain** device is a native MAGDA device (not part of the Faust FX bank) that sits under **Dynamics** in the Plugin Browser. It is a MIDI-triggered volume shaper: the device holds its own gain at unity and a retriggerable curve ducks it toward silence, keyed from the notes of another track. This is the classic "sidechain pump" effect without a compressor — the curve *is* the gain envelope, so you draw exactly the ducking shape you want.

**Choosing the trigger source.** The device faceplate has no source picker; pick the source from the **sidechain button in the device header** (tooltip *Sidechain source*). It turns orange once a source is routed. Clicking it opens a menu with **None**, an **Audio Sidechain** section, and a **MIDI Source** section listing the other tracks; the Sidechain device keys off a track chosen under **MIDI Source**.

**Controls:**

| Control | Description |
|---|---|
| **Curve** | The gain envelope. Edit it directly to shape how the level ducks and recovers on each trigger. |
| **DEPTH** | How far the curve pulls the gain down, 0–100%. |
| **SYNC** | Trigger sync division, which also sets the length of the duck curve. |
| **MODE** | **1-Shot** (run the curve once per trigger) or **Loop** (repeat it). |
| **CH** | **ST** ducks the full stereo signal; **SD** ducks the side channel only. |
| **ATK** | Attack, 0–50 ms. |
| **REL** | Release, 0–500 ms. |

!!! note "Placement"
    The Sidechain device can go on the master track as a *destination*, but the master can never be a *source* (it has no notes to key from). The device cannot run in the post-FX area, since ducking has to happen before the fader.

## Faust (Custom DSP)

The **Faust** device hosts a [Faust](https://faust.grame.fr) DSP that you compile and load at runtime. Unlike the rest of the MAGDA FX bank, where each device wraps a fixed pre-compiled `.dsp` source, this device accepts any Faust program.

Both this and the [Faust Instrument](faust-instrument.md) live under **Custom DSP** in the device browser. Reach for the instrument instead when you want MIDI to play your DSP as a synth rather than process an incoming signal.

- **Folder icon** - load a `.dsp` file from disk. The source is compiled and swapped in immediately.
- **Script icon** - opens an in-app code editor for live editing. Saving recompiles and hot-swaps the DSP.
- The current script name is shown in the header banner ("Drive" in the example).
- Arrows in the header step through parameter pages when the DSP exposes more controls than fit on one screen.

Parameters live in a stable pool of slots that persist across recompiles, so macro links, modulator routings, MIDI Learn assignments, and automation lanes survive a code change as long as the slot ordering is preserved. This makes the device practical for iterative DSP development without losing your patch state on every save.

### How your code is run

Your DSP is compiled to WebAssembly and then JIT-compiled to native machine code by [wasmtime](https://wasmtime.dev), so it runs as real compiled code rather than being interpreted.

Two things follow from that. It is fast enough to play with rather than merely audition, though it still carries more overhead than the ahead-of-time compiled MAGDA FX bank, which has no sandbox boundary and is optimised by your system compiler. And it is sandboxed: the DSP runs inside the WebAssembly memory model, so a patch that misbehaves cannot take the application down with it. That matters most when you are iterating quickly or loading code an AI wrote.

!!! note "Still evolving"
    This device is newer than the rest of the FX bank and its surface is still settling. For a patch you rely on in finished work, ask for it to be promoted into a compiled device.

### Writing Faust with AI help

[`faust-mcp-magda`](https://github.com/Conceptual-Machines/faust-mcp-magda) is an optional companion tool that exposes the Faust compiler and standard library to AI assistants. MAGDA uses it to validate AI-generated Faust code before loading it into this device, so syntax errors and bad library references get caught up front instead of failing at compile time.

Enable it from **Settings > AI Settings**, on the **Config** page, by turning on the **Faust DSP** toggle. When Faust validation is used, MAGDA will start the MCP server on-demand via `npx`; no manual install step is needed. `npx` must be available on the system `PATH` (it ships with Node.js).

The MCP server carries its own copy of the Faust compiler, so no separate Faust installation is required either. This is a separate thing from how the device runs your DSP during playback, described above; the server only checks that code compiles before MAGDA loads it.

The server can also be wired into a separate MCP-aware AI assistant (Claude, Cursor, and similar) directly, by adding it to that client's MCP config; see the [project README](https://github.com/Conceptual-Machines/faust-mcp-magda) for details. This is independent of the MAGDA-side toggle.
