declare name "MagdaKick";
declare description "Old-school drum-machine kick (808/909 lineage): a phase-reset pitched sine sweep into a saturator (inlined from Faust synths.lib sy.kick), plus a short noise click transient for the beater attack. Knob-tuned; the played MIDI note only gates the voice. Pitch/Sweep/Attack/Decay/Drive/Click are host macros.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls
// ============================================================================
// mydsp_poly drives these from note/velocity/gate. The drum is knob-tuned, so
// `freq` is intentionally unused (Faust elides it); only gate + velocity matter.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N], harvested + fanned to every voice)
// ============================================================================
// Time controls are in milliseconds (* 0.001 converts to the seconds sy.kick
// expects, same convention as magda_fm.dsp).
pitch    = hslider("Pitch [idx:0]",  55,  30, 120,  0.01);
sweep    = hslider("Sweep [idx:1]",  60,  5,  1000, 0.1) * 0.001;
attack   = hslider("Attack [idx:2]", 5,   5,  400,  0.1) * 0.001;
decay    = hslider("Decay [idx:3]",  500, 1,  4000, 1) * 0.001;
drive    = hslider("Drive [idx:4]",  2.0, 1.0, 10.0, 0.01);
clickAmt = hslider("Click [idx:5]",  0.3, 0.0, 1.0,  0.001);

// ============================================================================
// Voice
// ============================================================================
// Inlined from synths.lib sy.kick, but with a phase-reset sine so every hit
// starts at phase 0 (sy.kick's os.osc free-runs, giving an inconsistent
// transient hit-to-hit - audible at these low pitches). Reset fires for one
// sample on the gate's rising edge, same idiom as magda_polysynth.dsp. Sweep is
// the pitch-envelope decay (how fast the pitch drops).
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));
env      = en.adsr(attack, decay, 0.0, 0.1, gate);
pitchenv = en.adsr(0.005, sweep, 0.0, 0.1, gate);
osc      = sinR((1 + pitchenv * 4) * pitch);

// Click: a short high-passed noise transient (the beater tick), ~4 ms, fixed
// fast so it stays a tick regardless of the body envelope. Click sets the level.
clickEnv = en.ar(0.0002, 0.004, gate);
click    = (no.noise : fi.highpass(2, 2000)) * clickEnv * clickAmt;

voice   = ma.tanh((env * osc + click) * drive) * gain;
process = voice <: _, _;
