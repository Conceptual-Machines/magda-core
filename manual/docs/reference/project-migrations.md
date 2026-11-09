# Project Migrations

When MAGDA changes a device in a way that would make an older project sound different, it converts the project as it opens it. This page lists every such conversion, so you know what changed and what to listen for when you open an older project.

Presets saved by an older version of a device are converted too, when you load them.

## Projects from MAGDA v0

A project saved with a 0.x version of MAGDA opens with a prompt, **Save as a new v1 project?** Choose **Save v1 copy** to save it as a new project beside the original. The new project gets its own copy of the media, and the original is left alone so you can keep using it in v0. Projects saved with v1 may not open correctly in v0.

If you choose **Not now**, the project stays open, but **Save** won't write over the v0 project. Use **Save As** and pick a new location when you're ready.

The conversions further down this page are made as the project opens. Because the v0 project can't be saved over, the original keeps its old devices and settings.

## 4OSC becomes Poly Synth

4OSC has been replaced by [Poly Synth](../devices/poly-synth.md). When a v0 project has a 4OSC in it, the prompt says so, and saving the v1 copy turns each 4OSC into a Poly Synth carrying its oscillators, filter, both envelopes, voice settings and built-in effects.

- **Unison**: the oscillator with the most unison voices sets Poly Synth's shared voice count, rounded to the nearest of 2, 4 or 8. Each oscillator's level is trimmed to make up for the difference in how the two synths mix their voices.
- **Settings with no equivalent** are named in the prompt before you convert, so you know what to listen for.
- **Automation, macro and mod links** that addressed 4OSC's parameters are removed, along with project controller bindings to them. Poly Synth's parameters are different controls, and a link left in place would drive the wrong one.

## Retired effects

Earlier versions included a set of stock effects that have since been replaced by MAGDA's own devices. A project naming one of them loads its successor, with the saved settings converted:

| Saved as | Loads as |
|---|---|
| 4-band EQ | [EQ](../devices/effects.md) |
| Compressor | Compressor |
| Delay | Delay |
| Chorus | Chorus |
| Phaser | Phaser |
| Reverb | Reverb |
| Pitch Shift | Pitch |
| Lowpass | Filter |
| Volume/Pan | Utility |
| IR Reverb | Convolution |

Values are converted in real units (Hz, dB, ms, ratio), so each successor starts where its predecessor left off. A few controls have no counterpart and are dropped: the EQ's phase invert, the compressor's sidechain gain and trigger, the reverb's freeze, and Volume/Pan's polarity.

**IR Reverb** keeps its impulse response, and its automation and macro and mod links still work, because Convolution has the same parameters in the same order. For the other effects, automation and links that addressed the old device are removed: their parameters don't line up with the successor's.

## Parameter changes

| Device | What changed | What happens to an older project |
|---|---|---|
| EQ | Each band gained an **Enabled** switch at the front of its controls. | Values, automation and links move to the right controls, and all eight bands are switched on, as they were in the old device. |
| Limiter | **Hold**, **Mix** and **Autogain** were removed. | Threshold, Attack, Release and Output keep their values and links. Automation and links on the removed controls are dropped. |

## Lists that gained choices

Some switches gained new choices at the end of their list. A saved setting keeps its choice, and automation and macro and mod links are rescaled so they still land on the same choices as before.

| Device | Control | Added |
|---|---|---|
| Filter | Engine | Comb |
| Filter | Mode (SVF engine) | Notch+ |
| Poly Synth | Filter Slope, now poles | 2, 4 or 6 poles replace 12 and 24 dB. A filter saved at 24 dB plays as 4 poles. |

## Drum Grid pad buses

Drum Grid pads used to send to shared pad buses. Each pad holding a sound is now a [mixer channel](../devices/drum-grid.md) of its own. In an older project:

- a bus that carried one pad becomes that pad's channel, keeping its volume, pan, mute and solo;
- a bus that carried several pads becomes a group track, and the pads' channels route into it;
- a bus that carried nothing is removed.

## Devices this build doesn't have

A project can name a MAGDA device that your copy of MAGDA doesn't have, for example one from a newer version. That device isn't converted or removed. Its track is disabled and the device keeps its settings, so the project opens as it was in a copy of MAGDA that has it. See [Disabling a Track](../tracks.md#disabling-a-track).
