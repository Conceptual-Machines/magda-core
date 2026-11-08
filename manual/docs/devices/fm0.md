# FM0

FM0 is a 16-voice, four-operator FM synthesizer built into MAGDA. Every operator can phase-modulate any operator through a full 4x4 modulation matrix, including self-feedback on the diagonal. Each operator has its own ratio, level, and waveform, feeding a shared amplitude ADSR. An optional filter can sit after the operators or join the matrix as a fifth node. It plays in poly, mono, or legato modes.

## Faceplate

The faceplate has three panels:

- **OPS**, **AMP ENV**, and **VOICE** - three tabs sharing the first panel: the operators, the amplitude envelope, and the performance controls
- **MATRIX** - the modulation amounts
- **FILTER** and **FILTER ENV** - the filter and its envelope

The sidebar's **Show the operators** toggle hides the first panel and **Show the matrix** hides the matrix, to make the device narrower.

## Operators

The **OPS** tab has a row per operator, under the column heads **WAVE**, **RATIO**, **LEVEL**, and **RST**.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Ratio** | 0.25 to 16 | 1 (Op2 = 2) | Frequency ratio, a multiple of the played note |
| **Level** | -60 to 6 dB | 0 (Op2-4 = -60) | Operator output level |
| **Wave** | Sine / Triangle / Saw / Square / Noise | Sine | Operator waveform |

Click the waveform's icon to pick another wave from a menu. The dot beside it turns the operator on and off (on by default); a disabled operator stops both sounding and modulating, and its row greys out. Tick **RST** to reset the operator's phase on note-on.

The last row, **Filter**, holds the filter's **F Level** (see [The filter in the matrix](#the-filter-in-the-matrix)).

## Modulation matrix

The **MATRIX** panel routes operator outputs into operator phase inputs. Rows are sources, labelled **1** to **4**, and columns are destinations, labelled **→1** to **→4**. The cell in row 2, column **→1** is "operator 2 modulates operator 1"; its parameter is named **M21**. The diagonal cells (M11, M22, M33, M44) are self-feedback.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **M11 ... M44** | 0 to 8 | 0 (M21 = 2) | Modulation amount for each matrix cell |

A default patch has operator 2 modulating operator 1, the classic two-operator FM voice. Raise the diagonal for brighter, more aggressive timbres.

The extra row and column marked **F**, in blue, belong to the filter, and act only while it sits in the matrix.

## Filter

The filter is off by default. Turn it on with the dot on the **FILTER** tab. Switching it on or off, or changing its engine, type, or poles, while a note plays fades rather than clicking.

The icons at the right of the tabs choose its position: on the output, where it filters the operators' mix, or in the matrix (see below). Under them, pick the engine, **SVF**, **Ladder**, or **Comb**. The SVF adds a type, **LP**, **HP**, **BP**, or **Notch**, picked from icons, and **Poles** (2, 4, or 6) for its steepness. The comb has **+** and **-** modes instead. A response curve sits beside the tiles.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Cutoff** | 20 Hz to 20 kHz (log) | 2000 | Filter frequency |
| **Reso** | 0 to 1 | 0.3 | Resonance |
| **Drive** | 0 to 1 | 0 | Pre-filter saturation |
| **Key** | 0 to 1 | 0 | Octaves of cutoff per octave of the played note, around middle C |
| **Env Amt** | -4 to 4 oct | 0 | Filter envelope depth on cutoff (bipolar) |
| **Vel>Cut** | 0 to 6 oct | 0 | Velocity to cutoff depth |

The **FILTER ENV** tab shows the filter's own ADSR as a graph above four tiles, with the same ranges as the [amplitude envelope](#amplitude-envelope) and the same defaults.

### The filter in the matrix

In the matrix position the filter becomes a fifth node:

- The **→F** column sends operators into the filter, 0 to 1 each (M1F to M4F).
- The **F** row lets the filter's output phase-modulate the operators, 0 to 8 each like the matrix (MF1 to MF4).
- **F Level** (-60 to 6 dB, default 0) adds the filter's output to the mix.

The **FILTER** tab lists the operators sending to it, such as *IN 1 2*. In the output position the sends, returns, and **F Level** do nothing.

## Amplitude envelope

The **AMP ENV** tab shows the envelope as a graph above four tiles. Drag the handles on the graph, or edit the tiles.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 1 to 2000 ms | 5 | ADSR attack |
| **Decay** | 1 to 2000 ms | 300 | ADSR decay |
| **Sustain** | 0 to 1 | 0.5 | Sustain level |
| **Release** | 1 to 4000 ms | 400 | ADSR release |

## Voice

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Voice Mode** | Poly / Mono / Legato | Poly | Voicing |
| **Glide** | 0 to 2000 ms | 0 | Portamento (mono and legato only) |
| **Bend** | 0 to 24 st | 2 | Pitch-wheel range |
| **Gain** | -24 to 6 dB | -6 | Output level |
| **Vel>Amp** | 0 to 1 | 1 | Velocity sensitivity depth |

The pitch wheel moves every operator together, so the ratios hold.

## MPE

FM0 responds to per-note expression, as an MPE clip plays it (see [Pitch Glides](../panels/piano-roll.md#pitch-glides-mpe)):

- **Pitch bend** on a member channel (2 to 16) moves only the note on that channel, at a fixed range of ±48 semitones.
- **Timbre** (CC74) scales the modulation amounts, from half to double.
- **Pressure** lifts the note's level by up to 6 dB.

The pitch wheel on channel 1 is the ordinary wheel: it moves every note, by **Bend**. A keyboard set to send on any other channel has its wheel read as per-note bend at ±48 semitones, so set it to channel 1 to play with **Bend**.

## Macros, modulation, and presets

FM0 sits in the standard device slot, so its parameters accept [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).

## AI sound design

Click the **AI** icon in the device header and describe the sound you want. The agent reads this device's own parameters and their real-world ranges, then writes values into them. See [Per-Device AI Panel](../panels/ai-assistant.md#per-device-ai-panel).
