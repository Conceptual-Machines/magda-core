# Analysis (Oscilloscope & Spectrum)

MAGDA ships two real-time analysis devices, an **Oscilloscope** and a **Spectrum Analyzer**. They tap the signal passing through them and draw it; they do not change the audio. Drag either from the Plugin Browser onto any track's FX chain, or add it to the [post-FX area](../fx-chain.md#post-fx-section) of a track or the master.

Both devices also appear as compact [mini analyzers](../mixer-view.md#mini-analyzers) on the mixer strips, and either can be popped out into its own always-on-top window from the **open-in-window** button in the device header.

## Oscilloscope

The Oscilloscope draws the waveform in the time domain. It triggers on a zero crossing so the trace holds still instead of scrolling, and renders a stable min/max envelope of the signal.

| Control | What it does |
|---------|--------------|
| **Timebase** | How much time the display spans, from a few milliseconds up to a few seconds. Shorter shows individual cycles; longer shows the overall envelope. |
| **Trace colour** | Pick the waveform colour from a small set of swatches. The choice is saved with the device. |

dBFS reference lines mark the amplitude scale so you can read level at a glance.

## Spectrum Analyzer

The Spectrum Analyzer draws the frequency content of the signal. It runs a windowed FFT and plots magnitude against a logarithmic frequency axis.

| Control | What it does |
|---------|--------------|
| **FFT size** | Resolution of the analysis. Larger gives finer frequency detail at the cost of a slower response. |
| **Slope** | Tilts the display by a fixed dB-per-octave amount, so a natural spectrum reads flatter and high-frequency detail is easier to see. |
| **Smoothing** | How quickly the curve reacts — slower smoothing is steadier, faster is more responsive. |
| **Trace colour** | Pick the curve colour from a small set of swatches. |

A **peak-hold** line sits on top of the live curve, marking the highest level reached in each band. The frequency axis runs across the audible range with a dB magnitude scale down the side.

!!! note
    Both analyzers refresh continuously and add no latency or gain to the signal, so you can leave them inserted anywhere in a chain. In the post-FX area, only one Oscilloscope and one Spectrum Analyzer can exist per track.
