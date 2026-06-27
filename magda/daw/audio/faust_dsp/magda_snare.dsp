declare name "MagdaSnare";
declare description "Synthetic snare in three layers: a Transient (a pitched sine sweep with a super-sharp pitch env and very short decay, plus a high-passed noise crack), a tuned two-partial Body with a fast pitch snap, and a band-passed + resonant-high-passed noise Rattle/tail with drive. The body auto-ducks under the transient. Knob-tuned; the played MIDI note only gates the voice.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]) - grouped Transient / Body / Rattle.
// Time controls are in milliseconds (* 0.001 -> the seconds en.ar expects).
// ============================================================================
// Transient (pitched sine sweep + noise blend)
transAmt   = hslider("Transient [idx:0]",    0.5,  0.0,  1.0,   0.001);
transPitch = hslider("Trans Pitch [idx:1]",  400,  100,  2000,  1);
transSweep = hslider("Trans Sweep [idx:2]",  0.5,  0.0,  1.0,   0.001);
transDec   = hslider("Trans Decay [idx:3]",  10,   1,    100,   0.1) * 0.001;
transTone  = hslider("Trans Tone [idx:4]",   4000, 1000, 12000, 1);
// Body
tune      = hslider("Tune [idx:5]",          180,  100,  400,   0.1);
snap      = hslider("Snap [idx:6]",          0.25, 0.0,  1.0,   0.001);
snapTime  = hslider("Snap Time [idx:7]",     12,   2,    80,    0.1) * 0.001;
attack    = hslider("Attack [idx:8]",        0,    0,    100,   0.1) * 0.001;
bodyDec   = hslider("Body Decay [idx:9]",    180,  1,    1500,  1) * 0.001;
// Rattle / tail
snappy    = hslider("Snappy [idx:10]",       0.6,  0.0,  1.0,   0.001);
tone      = hslider("Tone [idx:11]",         3000, 800,  12000, 1);
hpFreq    = hslider("HP Freq [idx:12]",      300,  20,   6000,  1);
hpReso    = hslider("HP Reso [idx:13]",      0.7,  0.5,  10.0,  0.01);
rattleDec = hslider("Rattle Decay [idx:14]", 200,  1,    1500,  1) * 0.001;
drive     = hslider("Drive [idx:15]",        1.0,  1.0,  20.0,  0.01);
curve     = hslider("Curve [idx:16]",        0,   -50,   50,    1);
transCurve = hslider("Trans Curve [idx:17]", 0,   -50,   50,    1);
rattleCurve = hslider("Rattle Curve [idx:18]", 0, -50,   50,    1);

// ============================================================================
// Voice: Transient + Body + Rattle, summed and soft-clipped.
// ============================================================================
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));

// Transient: a pitched sine sweep is the main element - super-sharp fixed pitch
// env (~3 ms) over Trans Sweep depth (*16, a big drop), very short amp decay
// (Trans Decay). A high-passed noise crack is blended in underneath.
transAEnv  = en.ar(0.0002, transDec, gate);
transPEnv  = en.ar(0.0001, 0.003, gate);
transFreq  = transPitch * (1 + transPEnv * transSweep * 16);
transCurveExp = pow(8.0, -transCurve / 50.0);
transAEnvS = pow(transAEnv, transCurveExp);
transSine  = sinR(transFreq) * transAEnvS;
transNoise = (no.noise : fi.highpass(2, transTone)) * transAEnvS * 0.6;
trans      = (transSine + transNoise) * transAmt;

// Body: two tuned partials with a fast pitch snap (Snap*8 -> up to ~9x tune).
// Phase-reset for a consistent transient. The body auto-ducks under the
// transient (carve scales with Transient amount) so the crack punches through.
bodyEnv  = en.ar(attack, bodyDec, gate);
pitchEnv = en.ar(0.0005, snapTime, gate);
f0       = tune * (1 + pitchEnv * snap * 8);
partials = sinR(f0) * 0.8 + sinR(f0 * 1.59) * 0.5;
carve    = 1.0 - transAEnv * transAmt;
// Curve shapes the (linear) body decay: bipolar -50..50 knob -> exponent
// 8^(-c/50), 0 = linear, >0 swelled, <0 fast.
curveExp = pow(8.0, -curve / 50.0);
body     = partials * pow(bodyEnv, curveExp) * carve;

// Rattle / tail: band-passed noise -> resonant high-pass -> tanh drive, with its
// own decay. Snappy crossfades body <-> rattle.
rattleCurveExp = pow(8.0, -rattleCurve / 50.0);
rattleEnv = pow(en.ar(0.001, rattleDec, gate), rattleCurveExp);
rattle    = (no.noise : fi.resonbp(tone, 0.8, 1.0) : fi.resonhp(hpFreq, hpReso, 1.0)) * rattleEnv;
// Rattle ducks under the transient (full, scaled by Transient amount) and a bit
// under the body, so the crack and body punch through the noise tail.
rattleCarve = (1.0 - transAEnv * transAmt) * (1.0 - 0.3 * bodyEnv);
rattleOut = ma.tanh(rattle * drive) * rattleCarve;

voice   = ma.tanh(body * (1.0 - 0.5 * snappy) + rattleOut * snappy * 1.5 + trans) * gain;
process = voice <: _, _;
