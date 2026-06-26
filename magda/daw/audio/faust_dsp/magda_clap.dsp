declare name "MagdaClap";
declare description "Old-school drum-machine clap: four offset noise bursts through a resonant lowpass (Faust synths.lib sy.clap). Knob-tuned; the played MIDI note only gates the voice. Tone/Attack/Decay are host macros.";

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
// Time controls are in milliseconds (* 0.001 converts to the seconds sy.clap
// expects, same convention as magda_fm.dsp).
tone   = hslider("Tone [idx:0]",   1500, 400, 3500, 1);
attack = hslider("Attack [idx:1]", 0,    0,   200,  0.1) * 0.001;
decay  = hslider("Decay [idx:2]",  0,    0,   2000, 1) * 0.001;

// ============================================================================
// Voice
// ============================================================================
voice   = sy.clap(tone, attack, decay, gate) * gain;
process = voice <: _, _;
