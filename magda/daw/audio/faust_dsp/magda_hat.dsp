declare name "MagdaHat";
declare description "Old-school drum-machine hi-hat: phase-modulated metallic tone through a resonant lowpass (Faust synths.lib sy.hat). One device covers closed and open: a short Decay is a closed hat, a long Decay is an open hat. Knob-tuned; the played MIDI note only gates the voice. Pitch/Tone/Attack/Decay are host macros.";

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
// Time controls are in milliseconds (* 0.001 converts to the seconds sy.hat
// expects, same convention as magda_fm.dsp).
pitch  = hslider("Pitch [idx:0]",  800,  317, 3170,  1);
tone   = hslider("Tone [idx:1]",   8000, 800, 18000, 1);
attack = hslider("Attack [idx:2]", 5,    5,   200,   0.1) * 0.001;
decay  = hslider("Decay [idx:3]",  100,  1,   4000,  1) * 0.001;

// ============================================================================
// Voice
// ============================================================================
voice   = sy.hat(pitch, tone, attack, decay, gate) * gain;
process = voice <: _, _;
