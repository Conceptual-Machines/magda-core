# Drum Kit

The MAGDA drum kit is five synthetic drum voices: **Kick**, **Snare**, **Hat**, **Tom**, and **Clap**. Every voice is synthesised, not sampled. Drop them onto [Drum Grid](drum-grid.md) pads to build a kit, or add any one to a track and play it standalone.

All five are 16-voice, MIDI-gated instruments. Kick, Snare, and Tom tune to the played note where noted; Hat and Clap have a fixed character set by their own controls.

## Common controls

Most voices expose **Curve** knobs (range -50 to 50) that shape the decay envelopes. At 0 the decay is linear, positive values swell and slow the tail, negative values make it faster and more concave.

## Kick

Three layers: a pitched sine **Transient** sweep, a tuned sine **Body** with pitch-snap into a saturator, and a noise **Click**.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Transient** | 0 to 1 | 0.5 | Transient sine level |
| **Trans Pitch** | 60 to 1000 Hz | 220 | Transient pitch |
| **Trans Sweep** | 0 to 1 | 0.5 | Transient downward pitch sweep depth |
| **Trans Decay** | 1 to 100 ms | 8 | Transient decay |
| **Pitch** | 30 to 120 Hz | 55 | Body fundamental |
| **Snap** | 0 to 1 | 0.5 | Body pitch-snap depth |
| **Snap Time** | 5 to 1000 ms | 60 | Pitch-snap time |
| **Attack** | 0 to 400 ms | 0 | Body attack |
| **Body** | 1 to 4000 ms | 500 | Body decay |
| **Drive** | 1 to 10 | 2 | Saturation (body and click) |
| **Click** | 0 to 1 | 0.3 | Click noise level |
| **Click Tone** | 500 to 12000 Hz | 2000 | Click high-pass cutoff |
| **Curve** / **Trans Curve** | -50 to 50 | 0 | Body / transient decay shape |

## Snare

Three layers: a **Transient** (pitched sine sweep plus a high-passed noise crack), a tuned **Body**, and a resonant-high-passed **Rattle** tail.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Transient** | 0 to 1 | 0.5 | Transient level |
| **Trans Pitch** | 100 to 2000 Hz | 400 | Sine sweep frequency |
| **Trans Sweep** | 0 to 1 | 0.5 | Pitch-sweep depth |
| **Trans Decay** | 1 to 100 ms | 10 | Transient decay |
| **Trans Tone** | 1000 to 12000 Hz | 4000 | Noise crack high-pass cutoff |
| **Tune** | 100 to 400 Hz | 180 | Body fundamental |
| **Snap** | 0 to 1 | 0.25 | Pitch-snap depth |
| **Snap Time** | 2 to 80 ms | 12 | Pitch-snap time |
| **Attack** | 0 to 100 ms | 0 | Body attack |
| **Body Decay** | 1 to 1500 ms | 180 | Body decay |
| **Snappy** | 0 to 1 | 0.6 | Blend from body (0) to rattle (1) |
| **Tone** | 800 to 12000 Hz | 3000 | Rattle band-pass centre |
| **HP Freq** | 20 to 6000 Hz | 300 | Rattle high-pass cutoff |
| **HP Reso** | 0.5 to 10 | 0.7 | Rattle high-pass resonance |
| **Rattle Decay** | 1 to 1500 ms | 200 | Rattle decay |
| **Drive** | 1 to 20 | 1 | Rattle saturation |
| **Curve** / **Trans Curve** / **Rattle Curve** | -50 to 50 | 0 | Per-layer decay shape |

## Hat

Two independent layers: a metallic additive **Ring** and a high-passed **Noise** sizzle. Short decay reads as a closed hat, long as an open hat.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Ring** | 0 to 1 | 0.6 | Metallic ring level |
| **Pitch** | 200 to 2000 Hz | 540 | Ring fundamental |
| **Spread** | 0.5 to 2 | 1 | Ring dissonance (1 nominal, above spreads, below is more harmonic) |
| **Ring Decay** | 0.01 to 2000 ms | 10 | Ring decay |
| **Noise** | 0 to 1 | 0.5 | Noise sizzle level |
| **HP Freq** | 800 to 18000 Hz | 8000 | Noise high-pass cutoff |
| **HP Reso** | 0 to 1 | 0 | Noise high-pass resonance |
| **Sat** | 0 to 1 | 0 | Noise saturation |
| **Noise Decay** | 5 to 2000 ms | 100 | Noise decay |
| **Ring Curve** / **Noise Curve** | -50 to 50 | 0 | Per-layer decay shape |

## Tom

Two layers: a tuned sine **Body** with a downward pitch sweep, and a high-passed **Noise** stick attack.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Tune** | 50 to 400 Hz | 120 | Body fundamental |
| **Bend** | 0 to 1 | 0.4 | Downward pitch-sweep depth |
| **Attack** | 0 to 100 ms | 0 | Body attack |
| **Body** | 5 to 2000 ms | 400 | Body decay |
| **Noise** | 0 to 1 | 0.3 | Noise attack level |
| **Tone** | 200 to 12000 Hz | 1500 | Noise high-pass cutoff |
| **Noise Decay** | 5 to 1000 ms | 60 | Noise decay |
| **Curve** / **Noise Curve** | -50 to 50 | 0 | Per-layer decay shape |

## Clap

Band-passed noise shaped by a fast three-peak flam envelope, followed by a diffuse tail.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Tone** | 400 to 4000 Hz | 1000 | Band-pass centre |
| **Spread** | 2 to 30 ms | 9 | Flam spread between peaks |
| **Decay** | 20 to 1500 ms | 200 | Envelope decay |
| **Tail** | 0 to 1 | 0.4 | Diffuse tail level |
| **HP Freq** | 500 to 8000 Hz | 2000 | Resonant high-pass cutoff |
| **HP Reso** | 0.5 to 10 | 1 | Resonant high-pass resonance |
| **Drive** | 1 to 20 | 1 | Saturation |
| **Curve** | -50 to 50 | 0 | Decay shape |

## Macros, modulation, and presets

Each drum voice is a standard MAGDA device, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md). On a Drum Grid, each pad chain can carry its own macro and modulator links.
