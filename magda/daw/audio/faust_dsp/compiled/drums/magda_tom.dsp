declare name "MagdaTom";
declare description "Synthetic tom in two layers: a tuned phase-reset sine Body with a downward pitch sweep, and a high-passed Noise stick/skin attack with its own level and decay. Knob-tuned; the played MIDI note only gates the voice.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]) - grouped Body / Noise.
// Time controls are in milliseconds (* 0.001 -> the seconds en.adsr/en.ar want).
// ============================================================================
// Body
tune     = hslider("Tune [idx:0]",   120, 50,  400,  0.1);
bend     = hslider("Bend [idx:1]",   0.4, 0.0, 1.0,  0.001);
attack   = hslider("Attack [idx:2]", 0,   0,   100,  0.1) * 0.001;
bodyDec  = hslider("Body [idx:3]",   400, 5,   2000, 1) * 0.001;
// Noise
noiseLvl = hslider("Noise [idx:4]",       0.3,  0.0, 1.0,   0.001);
tone     = hslider("Tone [idx:5]",        1500, 200, 12000, 1);
noiseDec = hslider("Noise Decay [idx:6]", 60,   5,   1000,  1) * 0.001;
curve    = hslider("Curve [idx:7]",       0,   -50,  50,    1);
noiseCurve = hslider("Noise Curve [idx:8]", 0, -50,  50,    1);

// ============================================================================
// Voice: pitch-swept sine Body + high-passed Noise attack.
// ============================================================================
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));

// Body: phase-reset sine with a downward pitch sweep (Bend sets the depth) under
// a percussive AR.
bodyEnv  = en.adsr(attack, bodyDec, 0.0, 0.1, gate);
pitchenv = en.adsr(0.002, bodyDec * 0.4, 0.0, 0.1, gate);
// Curve shapes the (linear) body decay: bipolar -50..50 knob -> exponent
// 8^(-c/50), 0 = linear, >0 swelled, <0 fast.
curveExp = pow(8.0, -curve / 50.0);
body     = sinR(tune * (1 + pitchenv * bend * 2)) * pow(bodyEnv, curveExp);

// Noise: high-passed stick/skin attack with its own decay.
noiseCurveExp = pow(8.0, -noiseCurve / 50.0);
noise = (no.noise : fi.highpass(2, tone)) * pow(en.ar(0.001, noiseDec, gate), noiseCurveExp) * noiseLvl;

voice   = ma.tanh(body + noise) * gain;
process = voice <: _, _;
