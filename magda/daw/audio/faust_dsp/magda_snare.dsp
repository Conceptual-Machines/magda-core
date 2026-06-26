declare name "MagdaSnare";
declare description "Synthetic snare in three layers: a short noise Transient (the stick crack), a tuned two-partial Body with a fast pitch snap, and a band-passed + resonant-high-passed noise Rattle/tail with drive. The body auto-ducks under the transient. Body and rattle have independent decays. Knob-tuned; the played MIDI note only gates the voice.";

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
// Transient
transAmt  = hslider("Transient [idx:0]",     0.4,  0.0,  1.0,   0.001);
transTone = hslider("Trans Tone [idx:1]",    4000, 1000, 12000, 1);
// Body
tune      = hslider("Tune [idx:2]",          180,  100,  400,   0.1);
snap      = hslider("Snap [idx:3]",          0.25, 0.0,  1.0,   0.001);
snapTime  = hslider("Snap Time [idx:4]",     12,   2,    80,    0.1) * 0.001;
attack    = hslider("Attack [idx:5]",        0,    0,    100,   0.1) * 0.001;
bodyDec   = hslider("Body Decay [idx:6]",    180,  1,    1500,  1) * 0.001;
// Rattle / tail
snappy    = hslider("Snappy [idx:7]",        0.6,  0.0,  1.0,   0.001);
tone      = hslider("Tone [idx:8]",          3000, 800,  12000, 1);
hpFreq    = hslider("HP Freq [idx:9]",       300,  20,   6000,  1);
hpReso    = hslider("HP Reso [idx:10]",      0.7,  0.5,  10.0,  0.01);
rattleDec = hslider("Rattle Decay [idx:11]", 200,  1,    1500,  1) * 0.001;
drive     = hslider("Drive [idx:12]",        1.0,  1.0,  20.0,  0.01);

// ============================================================================
// Voice: Transient + Body + Rattle, summed and soft-clipped.
// ============================================================================
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));

// Transient: a very short bright noise crack (the stick hit), ~4 ms.
transEnv = en.ar(0.0002, 0.004, gate);
trans    = (no.noise : fi.highpass(2, transTone)) * transEnv * transAmt;

// Body: two tuned partials with a fast pitch snap. Snap*8 -> up to ~9x tune at
// the peak (one snap = a strong downward sweep). Phase-reset for a consistent
// transient. Snap Time is the snap speed, Body Decay the amp tail.
bodyEnv  = en.ar(attack, bodyDec, gate);
pitchEnv = en.ar(0.0005, snapTime, gate);
f0       = tune * (1 + pitchEnv * snap * 8);
partials = sinR(f0) * 0.8 + sinR(f0 * 1.59) * 0.5;
// The body auto-ducks under the transient (scaled by Transient amount), so the
// crack punches through cleanly before the body swells in.
carve    = 1.0 - transEnv * transAmt;
body     = partials * bodyEnv * carve;

// Rattle / tail: band-passed noise -> resonant high-pass -> tanh drive, with its
// own decay. Snappy crossfades body <-> rattle.
rattleEnv = en.ar(0.001, rattleDec, gate);
rattle    = (no.noise : fi.resonbp(tone, 0.8, 1.0) : fi.resonhp(hpFreq, hpReso, 1.0)) * rattleEnv;
rattleOut = ma.tanh(rattle * drive);

voice   = ma.tanh(body * (1.0 - 0.5 * snappy) + rattleOut * snappy * 1.5 + trans) * gain;
process = voice <: _, _;
