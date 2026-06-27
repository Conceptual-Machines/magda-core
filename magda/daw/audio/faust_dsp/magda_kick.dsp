declare name "MagdaKick";
declare description "Synthetic kick in three layers: a Transient (a pitched sine sweep with a super-sharp pitch env and very short decay), a Body (a low phase-reset sine with its own Snap pitch envelope into a saturator), and a noise Click. The body auto-ducks under the transient. Knob-tuned; the played MIDI note only gates the voice.";

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
// Host macro controls ([idx:N]) - grouped Transient / Body / Click.
// Time controls are in milliseconds (* 0.001 -> the seconds en.ar/en.adsr want).
// ============================================================================
// Transient (pitched sine sweep)
transAmt   = hslider("Transient [idx:0]",    0.5, 0.0, 1.0,  0.001);
transPitch = hslider("Trans Pitch [idx:1]",  220, 60,  1000, 1);
transSweep = hslider("Trans Sweep [idx:2]",  0.5, 0.0, 1.0,  0.001);
transDec   = hslider("Trans Decay [idx:3]",  8,   1,   100,  0.1) * 0.001;
// Body
pitch     = hslider("Pitch [idx:4]",      55,  30, 120,  0.01);
snap      = hslider("Snap [idx:5]",       0.5, 0.0, 1.0,  0.001);
snapTime  = hslider("Snap Time [idx:6]",  60,  5,  1000, 0.1) * 0.001;
attack    = hslider("Attack [idx:7]",     0,   0,  400,  0.1) * 0.001;
bodyDec   = hslider("Body [idx:8]",       500, 1,  4000, 1) * 0.001;
drive     = hslider("Drive [idx:9]",      2.0, 1.0, 10.0, 0.01);
// Click
clickAmt  = hslider("Click [idx:10]",      0.3,  0.0, 1.0,   0.001);
clickTone = hslider("Click Tone [idx:11]", 2000, 500, 12000, 1);
curve     = hslider("Curve [idx:12]",      0,   -50,  50,    1);
transCurve = hslider("Trans Curve [idx:13]", 0,  -50,  50,    1);

// ============================================================================
// Voice: Transient + Body + Click. Phase-reset sines (consistent transient).
// ============================================================================
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));

// Transient: a pitched sine sweep - super-sharp fixed pitch env (~3 ms) over
// Trans Sweep depth (*16), very short amp decay (Trans Decay).
transAEnv = en.ar(0.0002, transDec, gate);
transPEnv = en.ar(0.0001, 0.003, gate);
transFreq = transPitch * (1 + transPEnv * transSweep * 16);
transCurveExp = pow(8.0, -transCurve / 50.0);
trans     = sinR(transFreq) * pow(transAEnv, transCurveExp) * transAmt;

// Body: low pitched sine with its Snap pitch env (Snap*8 -> up to ~9x pitch).
// Auto-ducks under the transient so the sweep punches through cleanly.
bodyEnv  = en.adsr(attack, bodyDec, 0.0, 0.1, gate);
pitchenv = en.adsr(0.005, snapTime, 0.0, 0.1, gate);
osc      = sinR((1 + pitchenv * snap * 8) * pitch);
carve    = 1.0 - transAEnv * transAmt;
// Curve shapes the (linear) body decay. The bipolar -50..50 knob maps to an
// exponent 8^(-c/50): 0 = linear, >0 swelled, <0 fast/concave.
curveExp = pow(8.0, -curve / 50.0);
bodySig  = osc * pow(bodyEnv, curveExp) * carve;

// Click: a short high-passed noise transient (the beater tick).
clickEnv = en.ar(0.0002, 0.004, gate);
click    = (no.noise : fi.highpass(2, clickTone)) * clickEnv * clickAmt;

// Drive saturates body + click (the 808 grit); the clean transient sits on top.
voice   = ma.tanh((bodySig + click) * drive + trans) * gain;
process = voice <: _, _;
