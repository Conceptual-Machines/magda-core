declare name "MagdaSnare";
declare description "Old-school drum-machine snare: two tuned partials with a fast downward pitch sweep (Snap) for the body, blended with a band-passed noise burst for the rattle. Body and rattle have independent decays. Knob-tuned; the played MIDI note only gates the voice. Tune/Tone/Snappy/Attack/Body Decay/Rattle Decay/Snap are host macros.";

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
// Time controls are in milliseconds (range + box); the * 0.001 converts to the
// seconds en.ar expects (same convention as magda_fm.dsp).
tune      = hslider("Tune [idx:0]",         180,  100, 400,   0.1);
tone      = hslider("Tone [idx:1]",         3000, 800, 12000, 1);
snappy    = hslider("Snappy [idx:2]",       0.6,  0.0, 1.0,   0.001);
attack    = hslider("Attack [idx:3]",       0,    0,   100,   0.1) * 0.001;
bodyDec   = hslider("Body Decay [idx:4]",   180,  1,   1500,  1) * 0.001;
rattleDec = hslider("Rattle Decay [idx:5]", 200,  1,   1500,  1) * 0.001;
snap      = hslider("Snap [idx:6]",         0.3,  0.0, 1.0,   0.001);
snapTime  = hslider("Snap Time [idx:7]",    12,   2,   80,    0.1) * 0.001;
hpFreq    = hslider("HP Freq [idx:8]",      300,  20,  6000,  1);
hpReso    = hslider("HP Reso [idx:9]",      0.7,  0.5, 10.0,  0.01);
drive     = hslider("Drive [idx:10]",       1.0,  1.0, 20.0,  0.01);

// ============================================================================
// Voice
// ============================================================================
// Body: two tuned partials (fundamental + ~1.59x) with a fast downward pitch
// sweep (Snap sets the depth - the membrane "snap") under its own amp AR.
// Phase-reset sine so every hit starts at phase 0 for a consistent transient
// (one-sample reset on the gate rising edge, same idiom as magda_polysynth.dsp).
gateRise = gate > gate';
sinR(f)  = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, gateRise));
bodyEnv  = en.ar(attack, bodyDec, gate);
// Fast pitch snap, independent of Body Decay so the drop stays snappy however
// long the body rings. Snap sets the depth, Snap Time the speed (~2-80 ms).
pitchEnv = en.ar(0.0005, snapTime, gate);
f0       = tune * (1 + pitchEnv * snap);
partials = sinR(f0) * 0.8 + sinR(f0 * 1.59) * 0.5;
body     = partials * bodyEnv;

// Rattle: band-passed noise then a resonant high-pass (HP Freq / HP Reso) to
// carve the low end and add bite, then tanh drive for a bit of grit, with its
// own independent decay. Drive applies post-envelope so the grit tracks the hit.
rattleEnv = en.ar(0.001 + attack, rattleDec, gate);
rattle    = (no.noise : fi.resonbp(tone, 0.8, 1.0) : fi.resonhp(hpFreq, hpReso, 1.0)) * rattleEnv;
rattleOut = ma.tanh(rattle * drive);

// Snappy crossfades body <-> rattle; tanh keeps the sum bounded.
voice   = ma.tanh(body * (1.0 - 0.5 * snappy) + rattleOut * snappy * 1.5) * gain;
process = voice <: _, _;
