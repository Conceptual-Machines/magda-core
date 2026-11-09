# Poly Synth

Poly Synth is a 16-voice subtractive synthesizer built into MAGDA. Four detunable oscillators, with up to eight stereo unison copies, feed two multimode state-variable filters in series or in parallel, followed by an amplitude ADSR. It plays in poly, mono, or legato modes.

## Faceplate

The faceplate has three panels, each with two tabs:

- **OSC** and **VOICE** - the oscillators, and unison with the performance controls
- **FILTER 1** and **FILTER 2** - the two filters, their routing, and a response curve
- **AMP ENV** and **FILTER ENV** - the two envelopes

The **Show the oscillators** toggle in the device's sidebar hides the first panel, OSC and VOICE together, to make the device narrower.

## Oscillators

The **OSC** tab has a row per oscillator, under the column heads **LVL**, **CRS**, **FINE**, and **RST**. Oscillators 2 to 4 start silent (level -60 dB), so a fresh patch sounds from Osc 1 alone until you bring the others up.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Wave** | Sine / Saw / Square / Triangle | Saw | Oscillator waveform |
| **Level** | -60 to 6 dB | -12 (Osc 1) | Oscillator gain; -60 dB is silent |
| **Coarse** | -24 to 24 st | 0 | Pitch in semitones |
| **Fine** | -100 to 100 cents | 0 | Pitch in cents |

The row starts with the waveform's icon; click it to pick another wave from a menu. The dot beside the icon turns the oscillator on and off (on by default); a disabled oscillator's row greys out. Tick **RST** to reset the oscillator's phase on every note-on, for a consistent attack.

## Unison and voice

The **VOICE** tab holds unison and the performance controls. **Poly**, **Mono**, and **Legato** run across the top.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Voice Mode** | Poly / Mono / Legato | Poly | Voicing |
| **Voices** | 1 / 2 / 4 / 8 | 1 | Unison copies of the oscillators |
| **Detune** | 0 to 100 cents | 20 | How far the copies spread either side of the played pitch |
| **Spread** | 0 to 1 | 0.5 | Stereo width of the copies |
| **Glide** | 0 to 2000 ms | 0 | Portamento time (mono and legato only) |
| **Bend** | 0 to 24 st | 2 | Pitch-wheel range |
| **Output** | -60 to 6 dB | 0 | Master output level |
| **Vel>Amp** | 0 to 1 | 1 | Velocity to amplitude depth |
| **Vel>Cut** | 0 to 6 oct | 0 | Velocity to filter cutoff depth, on both filters |

With **Voices** above 1, every note plays that many copies of all four oscillators, spaced evenly across the **Detune** range. **Spread** pans them to alternate sides, wider towards the outer copies. The copies are mixed so that more voices don't get louder. **Detune** and **Spread** dim while **Voices** is 1.

Unison costs CPU in proportion to the copies. In a measured worst case, all four oscillators and both filters at 6 poles, 16 held notes at 8 voices took about a quarter of one CPU core, against under a tenth with unison off.

## Filters

Poly Synth has two state-variable filters, on the **FILTER 1** and **FILTER 2** tabs. The dot on each tab turns that filter on or off; Filter 1 starts on and Filter 2 off. A filter that is off passes its input straight through.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Type** | LP / HP / BP / Notch | LP | Filter response, picked from icons |
| **Poles** | 2 / 4 / 6 | 2 | Filter steepness |
| **Cutoff** | 50 to 18000 Hz (log) | 3000 | Filter frequency |
| **Reso** | 0 to 0.95 | 0.3 | Resonance |
| **Env Amt** | -4 to 4 oct | 0 | Filter envelope depth on cutoff (bipolar) |
| **Drive** | 0 to 1 | 0 | Pre-filter saturation |

Each filter has its own set of these. More poles cut more steeply, with the resonance on the last stage only. Switching the type or the poles while a note plays fades between them rather than clicking. The response curve beside the tiles draws the selected tab's filter.

The two filters share the filter envelope and **Vel>Cut**.

### Routing

The icons at the right of the panel's tabs set the routing:

- **Serial** runs Filter 1 into Filter 2.
- **Parallel** feeds both filters from the oscillators and mixes them by **Blend**: 0 is Filter 1 alone, 1 is Filter 2 alone, 0.5 (the default) is an even mix. **Blend** dims in Serial.

Poles replace the 12 dB and 24 dB slopes of earlier versions. A patch saved at 24 dB now plays as 4 poles, with one resonant peak where it had two. When you open a project that used the 4OSC synth, MAGDA offers to convert each one to a Poly Synth; see [Project Migrations](../reference/project-migrations.md).

## Envelopes

The **AMP ENV** and **FILTER ENV** tabs each show an envelope graph above four tiles. Drag the handles on the graph, or edit the tiles.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 1 to 2000 ms | 5 | Envelope attack |
| **Decay** | 1 to 2000 ms | 200 | Envelope decay |
| **Sustain** | 0 to 1 | 0.7 | Sustain level |
| **Release** | 1 to 4000 ms | 400 | Envelope release |

Both envelopes have the same ranges and defaults. The amp envelope shapes each note's level; the filter envelope moves both filters' cutoffs by their **Env Amt**.

## MPE

Poly Synth responds to per-note expression, as an MPE clip plays it (see [Pitch Glides](../panels/piano-roll.md#pitch-glides-mpe)):

- **Pitch bend** on a member channel (2 to 16) moves only the note on that channel, at a fixed range of ±48 semitones.
- **Timbre** (CC74) moves both filters' cutoffs, up to 3 octaves either way.
- **Pressure** lifts the note's level by up to 6 dB.

The pitch wheel on channel 1 is the ordinary wheel: it moves every note, by **Bend**. A keyboard set to send on any other channel has its wheel read as per-note bend at ±48 semitones, so set it to channel 1 to play with **Bend**.

## Macros, modulation, and presets

Like every MAGDA instrument, Poly Synth lives in the standard device slot, so its parameters can be driven by [macros](../modulation/macros.md) and [modulators](../modulation/overview.md), and patches save and recall through the [preset system](../panels/browsers.md).

## AI sound design

Click the **AI** icon in the device header and describe the sound you want. The agent reads this device's own parameters and their real-world ranges, then writes values into them. See [Per-Device AI Panel](../panels/ai-assistant.md#per-device-ai-panel).
