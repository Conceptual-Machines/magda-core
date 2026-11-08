# Analysis (Oscilloscope, Spectrum & Levels)

MAGDA ships three real-time analysis devices: an **Oscilloscope**, a **Spectrum Analyzer**, and a **Levels** meter. They tap the signal passing through them and draw it; they do not change the audio. Drag any of them from the Plugin Browser onto any track's FX chain, or add them to the [post-FX area](../fx-chain.md#post-fx-section) of a track or the master.

The Oscilloscope and Spectrum Analyzer also appear as compact [mini analyzers](../mixer-view.md#mini-analyzers) on the mixer strips, and can be popped out into their own always-on-top windows from the **open-in-window** button in the device header.

In the device chain, the Oscilloscope's and Spectrum Analyzer's plots take the whole body. Click the **settings** gear in the device header to fade their control row in over the foot of the plot, and again to hide it. The pop-out windows keep the row in view.

## Oscilloscope

The Oscilloscope draws the waveform in the time domain. It locks onto a rising edge, placed between samples, so a steady tone holds still even with noise on it, down to 20 Hz. Busy material such as a drum mix scrolls along with the audio instead of holding and then jumping. Zoomed in, the trace is a smooth curve through the samples; zoomed out, it is a filled min/max envelope.

| Control | What it does |
|---------|--------------|
| **Time** | How much time the display spans, from 1 ms to 5 s. Drag the value or type one (`250 ms`, `1.2 s`). Shorter shows individual cycles; longer shows the overall envelope. |
| **Color** | Five dots: Green, Blue, Yellow, Red and Purple. Click one to pick the trace colour; the current one is ringed, and hovering a dot names it. The choice is saved with the device. |

Reference lines at 0, -3, -6 and -12 dBFS mark the amplitude scale so you can read level at a glance.

## Spectrum Analyzer

The Spectrum Analyzer draws the frequency content of the signal. It runs a windowed FFT on overlapping frames, averages them, and plots magnitude against a logarithmic frequency axis. Each pixel column shows the loudest frequency it covers, and where the analysis is coarser than the screen, as in the low end, a smooth curve runs between its points. A tone reads at its true level wherever it falls.

| Control | What it does |
|---------|--------------|
| **FFT** | **2k**, **4k** or **8k**: the resolution of the analysis. Larger gives finer frequency detail, most useful in the low end, at the cost of a slower response. |
| **Slope dB/oct** | **0**, **3**, **4.5** or **6**: tilts the display by that many dB per octave, so a natural spectrum reads flatter and high-frequency detail is easier to see. |
| **Time** | **Slow**, **Med** or **Fast**: how quickly the curve reacts. Slower is steadier, faster is more responsive. |
| **Color** | Five dots, as on the Oscilloscope. |

A **peak-hold** line sits on top of the live curve, marking the highest level reached in each band. The frequency axis runs across the audible range with a dB magnitude scale down the side.

Hover over the plot for a readout of the frequency under the pointer, the nearest note with its offset in cents (440 Hz reads `A3 +0c`), and the level there.

### Track Overlay & Masking

The **Overlay** picker, in the device header (and at the top of the pop-out window), overlays another track's spectrum on the display as a neutral grey trace, so you can see how two tracks share the frequency range. When an overlay is active, MAGDA also runs its **masking detector** between the two tracks: frequency ranges where they fight for the same space are shaded in a warm tint behind the curves, with the shading intensity following the severity of the clash. Pick **No overlay** to remove it.

The same collision data appears in the mixer's [mix analysis](../mixer-view.md#mix-analysis) findings.

## Levels

The Levels meter gives broadcast-grade numbers for the signal at its insert point — drop it on the master for the mix, or on any track. Its readings sit in three columns, **LOUDNESS**, **PEAK / DYNAMICS** and **STEREO**:

| Reading | What it tells you |
|---------|--------------------|
| **Integrated**, **Short-term**, **Momentary** | Loudness in LUFS (BS.1770) — the perceived level streaming services normalise to. |
| **True peak** | Inter-sample peak, caught by oversampling — what a converter actually hits. |
| **PLR** | Peak-to-loudness ratio — how dynamic the signal is. |
| **PSR** | Peak-to-short-term ratio — how squashed the loudest moments are. |
| **Correlation** | Stereo phase relationship — negative values warn of mono-compatibility problems. |
| **Width** | Stereo width, from mono to fully wide. |

The device is a transparent pass-through, and only spends CPU on measurement while its meter is actually visible. Reopening it resets the integrated readings, and **RESET** restarts integrated loudness, peak hold and PLR at any time.

!!! note
    The analyzers refresh continuously and add no latency or gain to the signal, so you can leave them inserted anywhere in a chain. In the post-FX area, only one of each analysis device can exist per track, and an analyzer there has no power button or preset box, since it never changes the signal.
