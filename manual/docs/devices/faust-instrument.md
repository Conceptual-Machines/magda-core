# Faust Instrument

The **Faust Instrument** is the instrument counterpart to the [Faust effect](effects.md#faust-custom-dsp). Where that device runs a [Faust](https://faust.grame.fr) program as an insert on an audio signal, this one reports as a synth: it takes MIDI in, allocates voices, and plays your DSP once per held note.

Authoring is shared with the effect. You load a `.dsp` from disk with the folder icon, edit it live in the in-app code editor with the script icon, and the header shows the current patch name with arrows to step through parameter pages. Saving recompiles and hot-swaps the running DSP. Your code is compiled to WebAssembly and JIT-compiled to native machine code by wasmtime, sandboxed the same way, as described under [How your code is run](effects.md#how-your-code-is-run).

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

## Voice controls

Two host parameters sit on the **Voice** page, alongside whatever your patch declares:

- **Voice Mode** - **Poly** allocates a fresh voice per note. **Mono** plays one note at a time and retriggers the envelope on each new note. **Legato** also plays one note at a time, but overlapping notes slide to the new pitch without retriggering, so the envelope keeps its shape.
- **Glide** - portamento time, from instant up to 2 seconds. In **Legato** it gives the classic slide between overlapping notes, and releasing back to a note you are still holding glides back to it.

Both are ordinary automatable parameters, so they can be automated, linked to macros, and driven by modulators like any other control.

## Parameters across recompiles

Parameters live in the same stable slot pool the Faust effect uses, so macro links, modulator routings, MIDI Learn assignments, and automation lanes survive a recompile as long as slot ordering is preserved. You can keep editing the DSP without losing the patch you built around it.

## Saving patches

Patches you save from the editor go to a `FaustInstruments` folder in MAGDA's data directory, kept separate from the effect's `FaustEffects`. Patches saved before instruments and effects were split stay listed under effects; leaving them there breaks nothing.
