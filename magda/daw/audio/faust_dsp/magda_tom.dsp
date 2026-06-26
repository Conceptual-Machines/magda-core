declare name "MagdaTom";
declare description "Old-school drum-machine tom: a tuned sine with a downward pitch sweep and a percussive amp envelope. Knob-tuned; the played MIDI note only gates the voice. Tune/Bend/Attack/Decay are host macros.";

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
// Time controls are in milliseconds (* 0.001 converts to the seconds en.adsr
// expects, same convention as magda_fm.dsp).
tune   = hslider("Tune [idx:0]",   120, 50,  400,  0.1);
bend   = hslider("Bend [idx:1]",   0.4, 0.0, 1.0,  0.001);
attack = hslider("Attack [idx:2]", 0,   0,   100,  0.1) * 0.001;
decay  = hslider("Decay [idx:3]",  400, 5,   2000, 1) * 0.001;

// ============================================================================
// Voice: pitch-swept sine (Bend sets the sweep depth) under a percussive AR.
// ============================================================================
// Phase-reset sine so every hit starts at phase 0 for a consistent transient
// (one-sample reset on the gate rising edge, same idiom as magda_polysynth.dsp).
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));
env      = en.adsr(attack, decay, 0.0, 0.1, gate);
pitchenv = en.adsr(0.002, decay * 0.4, 0.0, 0.1, gate);
osc      = sinR(tune * (1 + pitchenv * bend * 2));

voice   = (osc * env) * gain;
process = voice <: _, _;
