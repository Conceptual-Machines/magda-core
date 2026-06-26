declare name "MagdaHat";
declare description "Synthetic hi-hat in two layers with independent controls: a metallic Ring (inharmonic additive partials, with a Spread/dissonance control) and a high-passed Noise sizzle, each with its own level and decay. Short decays = closed hat, long = open. Knob-tuned; the played MIDI note only gates the voice.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]) - grouped Ring / Noise.
// Time controls are in milliseconds (* 0.001 -> the seconds en.ar expects).
// ============================================================================
// Ring (inharmonic additive partials)
ringLvl   = hslider("Ring [idx:0]",       0.6,  0.0, 1.0,  0.001);
ringPitch = hslider("Pitch [idx:1]",      540,  200, 2000, 1);
spread    = hslider("Spread [idx:2]",     1.0,  0.5, 2.0,  0.001);
ringDec   = hslider("Ring Decay [idx:3]", 300,  10,  2000, 1) * 0.001;
// Noise
noiseLvl  = hslider("Noise [idx:4]",       0.5,  0.0, 1.0,   0.001);
tone      = hslider("Tone [idx:5]",        8000, 800, 18000, 1);
noiseDec  = hslider("Noise Decay [idx:6]", 100,  5,   2000,  1) * 0.001;

// ============================================================================
// Voice: metallic Ring + Noise sizzle.
// ============================================================================
// Inharmonic additive ring (sy.additiveDrum, sine partials). Spread scales each
// partial's deviation from the fundamental: 1 = nominal metallic, >1 more
// dissonant, <1 toward harmonic/bell. The fundamental (ratio 1) stays fixed.
sr(b)  = 1.0 + (b - 1.0) * spread;
ratios = (sr(1.0), sr(1.34), sr(1.81), sr(2.27), sr(2.67), sr(3.08));
gains  = (1.0, 0.8, 0.7, 0.6, 0.5, 0.45);
ring   = sy.additiveDrum(ringPitch, ratios, gains, 0.5, 0.001, ringDec, gate) * ringLvl;

// High-passed noise sizzle with its own decay.
noise  = (no.noise : fi.highpass(3, tone)) * en.ar(0.001, noiseDec, gate) * noiseLvl;

voice   = ma.tanh(ring + noise) * gain;
process = voice <: _, _;
