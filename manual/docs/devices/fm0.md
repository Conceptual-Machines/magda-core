# FM0

FM0 is a 16-voice, four-operator FM synthesizer built into MAGDA. Every operator can phase-modulate any operator through a full 4x4 modulation matrix, including self-feedback on the diagonal. Each operator has its own ratio, level, and waveform, feeding a shared amplitude ADSR.

## Modulation matrix

The 4x4 matrix routes operator outputs into operator phase inputs. Rows are sources, columns are destinations, so cell **M2>1** is "operator 2 modulates operator 1". The diagonal cells (M1>1, M2>2, M3>3, M4>4) are self-feedback.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **M1>1 ... M4>4** | 0 to 8 | 0 (M2>1 = 2) | Modulation amount for each matrix cell |

A default patch has operator 2 modulating operator 1, the classic two-operator FM voice. Raise the diagonal for brighter, more aggressive timbres.

## Operators

Each of the four operators has these controls.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Ratio** | 0.25 to 16 | 1 (Op2 = 2) | Frequency ratio, a multiple of the played note |
| **Level** | -60 to 6 dB | 0 (Op2-4 = -60) | Operator output level |
| **Wave** | Sine / Tri / Saw / Square / Noise | Sine | Operator waveform |

Each operator also has an **Enable** toggle (on by default; disabling stops it both sounding and modulating) and a **Reset** toggle that resets its phase on note-on.

## Amp, voice, and velocity

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Amp Attack** | 1 to 2000 ms | 5 | ADSR attack |
| **Amp Decay** | 1 to 2000 ms | 300 | ADSR decay |
| **Amp Sustain** | 0 to 1 | 0.5 | Sustain level |
| **Amp Release** | 1 to 4000 ms | 400 | ADSR release |
| **Glide** | 0 to 2000 ms | 0 | Portamento (mono only; poly forces 0) |
| **Vel Amount** | 0 to 1 | 1 | Velocity sensitivity depth |

## Macros, modulation, and presets

FM0 sits in the standard device slot, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).
