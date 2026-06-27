declare name "MagdaClap";
declare description "Synthetic clap: a short band-passed noise burst plus two delayed copies (Spread spacing) give the hand-clap flam, over a longer diffuse tail. Knob-tuned; the played MIDI note only gates the voice. Tone/Spread/Decay/Tail are host macros.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]). Spread is in milliseconds, Decay in ms.
// ============================================================================
tone    = hslider("Tone [idx:0]",   1200, 500, 3000, 1);
spread  = hslider("Spread [idx:1]", 9,    3,   30,   0.1) * 0.001;
decay   = hslider("Decay [idx:2]",  200,  20,  1000, 1) * 0.001;
tailLvl = hslider("Tail [idx:3]",   0.5,  0.0, 1.0,  0.001);

// ============================================================================
// Voice
// ============================================================================
// One short band-passed noise burst, then two delayed copies spaced by Spread
// give the clap flam (the multiple-hands slap-back); a longer band-passed tail
// fills the room.
maxD     = 8192;
oneBurst = (no.noise : fi.resonbp(tone, 2.0, 1.0)) * en.ar(0.0003, 0.007, gate);
sd       = spread * ma.SR;
bursts   = oneBurst + de.delay(maxD, sd, oneBurst) * 0.8 + de.delay(maxD, 2.0 * sd, oneBurst) * 0.6;
tail     = (no.noise : fi.resonbp(tone, 1.2, 1.0)) * en.ar(0.004, decay, gate);

voice   = ma.tanh(bursts * 0.8 + tail * tailLvl) * gain;
process = voice <: _, _;
