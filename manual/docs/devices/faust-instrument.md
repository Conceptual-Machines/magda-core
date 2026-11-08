# Faust Instrument

The **Faust Instrument** is the instrument counterpart to the [Faust effect](effects.md#faust-custom-dsp). Where that device runs a [Faust](https://faust.grame.fr) program as an insert on an audio signal, this one reports as a synth: it takes MIDI in, allocates voices, and plays your DSP once per held note.

Authoring is shared with the effect. The device's title is the patch's name; hover it to see the credits the patch declares (`declare author`, `version`, `license`, `description`). Load a `.dsp` from the **Patches** submenu of the preset box (see [Patches and presets](#patches-and-presets)). The code icon beside the floppy-disk button (*Edit the patch's source*) opens the in-app code editor: click **Compile** to recompile and hot-swap the running DSP. If a patch fails to compile, the error shows in a strip along the foot of the device.

Your code is compiled to WebAssembly and JIT-compiled to native machine code by wasmtime, sandboxed the same way, as described under [How your code is run](effects.md#how-your-code-is-run).

## Writing a polyphonic patch

The instrument follows the Faust polyphonic convention. Three control names are reserved:

| Control | Driven by |
|---------|-----------|
| `freq` | the pitch of the note being played, in Hz |
| `gain` | note velocity |
| `gate` | note on and note off, for triggering envelopes |

Declare them as ordinary sliders and read them in your DSP. MAGDA drives them per voice and does not expose them as user parameters, so they never appear as knobs or automation targets. Every other control you declare becomes a parameter as usual.

A minimal patch looks like this:

```faust
import("stdfaust.lib");

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

cutoff = hslider("Cutoff", 2000, 50, 12000, 1);

process = os.sawtooth(freq) * gain
        : fi.lowpass(2, cutoff)
        * en.adsr(0.01, 0.2, 0.7, 0.3, gate)
        <: _, _;
```

A patch that produces one output channel is fanned to both; there is no need to split it yourself.

The device plays up to 16 voices at once. When they are all busy, the oldest released voice is reused first, then the oldest still playing.

The parameters sit in a grid of up to 8 by 2 controls, on pages named for the groups in your patch and shown as tabs.

## Voice controls

Three host parameters sit on the **Voice** page, alongside whatever your patch declares:

- **Voice Mode** - **Poly** allocates a fresh voice per note. **Mono** plays one note at a time and retriggers the envelope on each new note. **Legato** also plays one note at a time, but overlapping notes slide to the new pitch without retriggering, so the envelope keeps its shape.
- **Glide** - portamento time, from instant up to 2 seconds. In **Legato** it gives the classic slide between overlapping notes, and releasing back to a note you are still holding glides back to it.
- **Bend Range** - how far the pitch wheel moves the note, in semitones each way, up to 24. The default of 2 matches most synths.

All three can be automated. **Glide** and **Bend Range** can also be linked to macros and driven by modulators like any other control; **Voice Mode** follows automation only.

Pitch bend is applied by MAGDA to the same `freq` your patch already reads, so the wheel works without your DSP doing anything to support it. There is no need to declare a bend control or tag anything with `[midi:pitchwheel]`. Stopping the transport recentres the wheel, so a bend held at the moment you stop does not carry over into what you play next.

## MPE

The instrument plays per-note expression, as an MPE clip plays it (see [Pitch Glides](../panels/piano-roll.md#pitch-glides-mpe)).

Pitch bend on a member channel (2 to 16) moves only the note on that channel, at a fixed range of ±48 semitones, and works for every patch through `freq`. The wheel on channel 1 is the ordinary wheel: it moves every note, by **Bend Range**. A keyboard set to send on any other channel has its wheel read as per-note bend.

Two more control names are reserved for patches that want them:

| Control | Driven by |
|---------|-----------|
| `timbre` | CC74 on the note's channel, 0 to 1, resting at the middle (64 of 127) |
| `pressure` | channel pressure on the note's channel, 0 to 1, resting at 0 |

Declare a `timbre` or `pressure` slider and each voice gets its own note's value. Like `freq`, `gain`, and `gate`, they stay out of the parameter list.

## Parameters across recompiles

Parameters live in the same stable slot pool the Faust effect uses, so macro links, modulator routings, MIDI Learn assignments, and automation lanes survive a recompile as long as slot ordering is preserved. You can keep editing the DSP without losing the patch you built around it.

## Patches and presets

A `.dsp` file holds source only. The preset box's **Patches** submenu handles them:

- **Built-in** - the instrument patches that ship with MAGDA, grouped by category.
- **My Patches** - the `.dsp` files in `Faust Patches/Instruments` in your presets folder (set in **Settings > Paths**). Instrument and effect patches are kept apart, since an instrument patch can't load into the effect.
- **Load .dsp File...** - open any `.dsp` from disk.
- **Save .dsp File...** - save the current source as a `.dsp`, into My Patches by default.

Loading a patch replaces the source and nothing else: your mods and macros stay, and the preset box clears its name, since no preset describes the device any more.

A MAGDA preset, saved with the floppy-disk button, is a full snapshot: the source, the parameter values, mods, macros, and gain. Loading one brings its patch back with it.

The `FaustInstruments` and `FaustEffects` folders earlier versions saved patches to are no longer listed. Their files stay on disk; open them with **Load .dsp File...**.
