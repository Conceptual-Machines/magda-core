declare name "MagdaSnare";
declare description "Old-school drum-machine snare: a tuned additive body (Faust synths.lib sy.additiveDrum) blended with a band-passed noise burst for the snares. Knob-tuned; the played MIDI note only gates the voice. Tune/Tone/Snappy/Attack/Decay are host macros.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N])
// ============================================================================
tune   = hslider("Tune [idx:0]",   180,   100,   400,   0.1);
tone   = hslider("Tone [idx:1]",   3000,  800,   12000, 1);
snappy = hslider("Snappy [idx:2]", 0.6,   0.0,   1.0,   0.001);
attack = hslider("Attack [idx:3]", 0.002, 0.001, 0.1,   0.001);
decay  = hslider("Decay [idx:4]",  0.2,   0.02,  1.5,   0.001);

// ============================================================================
// Voice: tuned two-mode body + band-passed noise snares, blended by Snappy.
// ============================================================================
body     = sy.additiveDrum(tune, (1, 1.58), (1, 0.7), 0.5, attack, decay, gate);
noiseEnv = en.ar(0.001 + attack, decay * 0.7, gate);
snares   = (no.noise : fi.resonbp(tone, 0.8, 1.0)) * noiseEnv;

voice   = ma.tanh(body * (1.0 - 0.5 * snappy) + snares * snappy * 1.5) * gain;
process = voice <: _, _;
