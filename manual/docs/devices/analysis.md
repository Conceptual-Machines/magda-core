# Analysis (Oscilloscope, Spectrum & Levels)

MAGDA ships three real-time analysis devices: an **Oscilloscope**, a **Spectrum Analyzer**, and a **Levels** meter. They tap the signal passing through them and draw it; they do not change the audio. Drag any of them from the Plugin Browser onto any track's FX chain, or add them to the [post-FX area](../fx-chain.md#post-fx-section) of a track or the master.

The Oscilloscope and Spectrum Analyzer also appear as compact [mini analyzers](../mixer-view.md#mini-analyzers) on the mixer strips, and can be popped out into their own always-on-top windows from the **open-in-window** button in the device header.

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

### Track Overlay & Masking

In the full editor or pop-out window, the **Overlay** selector overlays another track's spectrum on the display as a neutral grey trace, so you can see how two tracks share the frequency range. When an overlay is active, MAGDA also runs its **masking detector** between the two tracks: frequency ranges where they fight for the same space are shaded in a warm tint behind the curves, with the shading intensity following the severity of the clash. Pick **Off** to remove the overlay.

The same collision data appears in the mixer's [mix analysis](../mixer-view.md#mix-analysis) findings.

## Levels

The Levels meter gives broadcast-grade numbers for the signal at its insert point — drop it on the master for the mix, or on any track:

| Reading | What it tells you |
|---------|--------------------|
| **LUFS** | Integrated loudness (BS.1770) — the perceived level streaming services normalise to. |
| **True peak** | Inter-sample peak in dBTP, caught by oversampling — what a converter actually hits. |
| **PLR** | Peak-to-loudness ratio — how dynamic the signal is. |
| **PSR** | Peak-to-short-term ratio — how squashed the loudest moments are. |
| **Correlation** | Stereo phase relationship — negative values warn of mono-compatibility problems. |
| **Width** | Stereo width, from mono to fully wide. |

The device is a transparent pass-through, and only spends CPU on measurement while its meter is actually visible. Reopening it resets the integrated readings.

!!! note
    The analyzers refresh continuously and add no latency or gain to the signal, so you can leave them inserted anywhere in a chain. In the post-FX area, only one of each analysis device can exist per track.
