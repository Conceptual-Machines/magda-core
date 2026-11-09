# Drum Kit

The MAGDA drum kit is five synthetic drum voices: **Kick**, **Snare**, **Hat**, **Tom**, and **Clap**. Every voice is synthesised, not sampled. Drop them onto [Drum Grid](drum-grid.md) pads to build a kit, or add any one to a track and play it standalone.

All five are 8-voice, MIDI-gated instruments. The played note only triggers the hit: each voice is tuned by its own controls, not by the key you play.

## Faceplate

Each voice is built from layers, such as a kick's transient, body, and click. The layers sit behind tabs, and the selected tab shows that layer's controls.

Beside the controls, a plot draws the rendered hit, with each layer's envelope traced over it in its own colour; the selected layer's is bright and the others dimmed. Drag a layer's handles to set its attack, where the layer has one, and its decay. Scroll over the plot to bend the curve of the layer nearest the pointer.

The sidebar's **Show the parameters** toggle hides the controls, and the other toggle hides the plot. One of the two always shows.

There is no gain control on the faceplate: set the voice's level with the fader on the device's side strip.

### Curve

Layers with a decay have a **Curve** control (-50 to 50) that shapes it. At 0 the decay is linear, positive values swell and slow the tail, and negative values make it faster and more concave.

## Kick

Three layers: a pitched sine **TRANSIENT** sweep, a tuned sine **BODY** with pitch-snap into a saturator, and a noise **CLICK**.

| Tab | Control | Range | Default | Description |
|-----|---------|-------|---------|-------------|
| TRANSIENT | **Level** | 0 to 1 | 0.5 | Transient sine level |
| TRANSIENT | **Pitch** | 60 to 1000 Hz | 220 | Transient pitch |
| TRANSIENT | **Sweep** | 0 to 1 | 0.5 | Downward pitch-sweep depth |
| TRANSIENT | **Decay** | 1 to 100 ms | 8 | Transient decay |
| TRANSIENT | **Curve** | -50 to 50 | 0 | Transient decay shape |
| BODY | **Pitch** | 30 to 120 Hz | 55 | Body fundamental |
| BODY | **Snap** | 0 to 1 | 0.5 | Body pitch-snap depth |
| BODY | **Snap time** | 5 to 1000 ms | 60 | Pitch-snap time |
| BODY | **Attack** | 0 to 400 ms | 0 | Body attack |
| BODY | **Decay** | 1 to 4000 ms | 500 | Body decay |
| BODY | **Drive** | 1 to 10 | 2 | Saturation (body and click) |
| BODY | **Curve** | -50 to 50 | 0 | Body decay shape |
| CLICK | **Level** | 0 to 1 | 0.3 | Click noise level |
| CLICK | **Tone** | 500 to 12000 Hz | 2000 | Click high-pass cutoff |

## Snare

Three layers: a **TRANSIENT** (pitched sine sweep plus a high-passed noise crack), a tuned **BODY**, and a resonant-high-passed **RATTLE** tail.

| Tab | Control | Range | Default | Description |
|-----|---------|-------|---------|-------------|
| TRANSIENT | **Level** | 0 to 1 | 0.5 | Transient level |
| TRANSIENT | **Pitch** | 100 to 2000 Hz | 400 | Sine sweep frequency |
| TRANSIENT | **Sweep** | 0 to 1 | 0.5 | Pitch-sweep depth |
| TRANSIENT | **Decay** | 1 to 100 ms | 10 | Transient decay |
| TRANSIENT | **Tone** | 1000 to 12000 Hz | 4000 | Noise crack high-pass cutoff |
| TRANSIENT | **Curve** | -50 to 50 | 0 | Transient decay shape |
| BODY | **Tune** | 100 to 400 Hz | 180 | Body fundamental |
| BODY | **Snap** | 0 to 1 | 0.25 | Pitch-snap depth |
| BODY | **Snap time** | 2 to 80 ms | 12 | Pitch-snap time |
| BODY | **Attack** | 0 to 100 ms | 0 | Body attack |
| BODY | **Decay** | 1 to 1500 ms | 180 | Body decay |
| BODY | **Curve** | -50 to 50 | 0 | Body decay shape |
| RATTLE | **Snappy** | 0 to 1 | 0.6 | Blend from body (0) to rattle (1) |
| RATTLE | **Tone** | 800 to 12000 Hz | 3000 | Rattle band-pass centre |
| RATTLE | **HP freq** | 20 to 6000 Hz | 300 | Rattle high-pass cutoff |
| RATTLE | **HP reso** | 0.5 to 10 | 0.7 | Rattle high-pass resonance |
| RATTLE | **Decay** | 1 to 1500 ms | 200 | Rattle decay |
| RATTLE | **Drive** | 1 to 20 | 1 | Rattle saturation |
| RATTLE | **Curve** | -50 to 50 | 0 | Rattle decay shape |

## Hat

Two independent layers: a metallic additive **RING** and a high-passed **NOISE** sizzle. Short decay reads as a closed hat, long as an open hat.

| Tab | Control | Range | Default | Description |
|-----|---------|-------|---------|-------------|
| RING | **Level** | 0 to 1 | 0.6 | Metallic ring level |
| RING | **Pitch** | 200 to 2000 Hz | 540 | Ring fundamental |
| RING | **Spread** | 0.5 to 2 | 1 | Ring dissonance (1 nominal, above spreads, below is more harmonic) |
| RING | **Decay** | 0.01 to 2000 ms | 10 | Ring decay |
| RING | **Curve** | -50 to 50 | 0 | Ring decay shape |
| NOISE | **Level** | 0 to 1 | 0.5 | Noise sizzle level |
| NOISE | **HP freq** | 800 to 18000 Hz | 8000 | Noise high-pass cutoff |
| NOISE | **HP reso** | 0 to 1 | 0 | Noise high-pass resonance |
| NOISE | **Sat** | 0 to 1 | 0 | Noise saturation |
| NOISE | **Decay** | 5 to 2000 ms | 100 | Noise decay |
| NOISE | **Curve** | -50 to 50 | 0 | Noise decay shape |

## Tom

Two layers: a tuned sine **BODY** with a downward pitch sweep, and a high-passed **NOISE** stick attack.

| Tab | Control | Range | Default | Description |
|-----|---------|-------|---------|-------------|
| BODY | **Tune** | 50 to 400 Hz | 120 | Body fundamental |
| BODY | **Bend** | 0 to 1 | 0.4 | Downward pitch-sweep depth |
| BODY | **Attack** | 0 to 100 ms | 0 | Body attack |
| BODY | **Decay** | 5 to 2000 ms | 400 | Body decay |
| BODY | **Curve** | -50 to 50 | 0 | Body decay shape |
| NOISE | **Level** | 0 to 1 | 0.3 | Noise attack level |
| NOISE | **Tone** | 200 to 12000 Hz | 1500 | Noise high-pass cutoff |
| NOISE | **Decay** | 5 to 1000 ms | 60 | Noise decay |
| NOISE | **Curve** | -50 to 50 | 0 | Noise decay shape |

## Clap

Band-passed noise shaped by a fast three-peak flam envelope, followed by a diffuse tail, then driven. The **FLAM** tab holds the noise band and the envelope; the **SHAPE** tab holds the resonant high-pass and the drive.

| Tab | Control | Range | Default | Description |
|-----|---------|-------|---------|-------------|
| FLAM | **Tone** | 400 to 4000 Hz | 1000 | Band-pass centre |
| FLAM | **Spread** | 2 to 30 ms | 9 | Flam spread between peaks |
| FLAM | **Decay** | 20 to 1500 ms | 200 | Envelope decay |
| FLAM | **Tail** | 0 to 1 | 0.4 | Diffuse tail level |
| FLAM | **Curve** | -50 to 50 | 0 | Decay shape |
| SHAPE | **HP freq** | 500 to 8000 Hz | 2000 | Resonant high-pass cutoff |
| SHAPE | **HP reso** | 0.5 to 10 | 1 | Resonant high-pass resonance |
| SHAPE | **Drive** | 1 to 20 | 1 | Saturation |

## Macros, modulation, and presets

Each drum voice is a standard MAGDA device, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md). On a Drum Grid, each pad chain can carry its own macro and modulator links.

## AI sound design

Click the **AI** icon in the device header and describe the sound you want. The agent reads this device's own parameters and their real-world ranges, then writes values into them. See [Per-Device AI Panel](../panels/ai-assistant.md#per-device-ai-panel).
