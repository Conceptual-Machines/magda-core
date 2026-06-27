declare name "MagdaClap";
declare description "Synthetic clap: one band-passed noise source shaped by a fast three-triangle amplitude envelope (the hand-clap flam), then a diffuse tail. Knob-tuned; the played MIDI note only gates the voice. Tone/Spread/Decay/Tail are host macros.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]). Spread / Decay are in milliseconds.
// ============================================================================
tone    = hslider("Tone [idx:0]",    1000, 400, 4000, 1);
spread  = hslider("Spread [idx:1]",  9,    2,   30,   0.1) * 0.001;
decay   = hslider("Decay [idx:2]",   200,  20,  1500, 1) * 0.001;
tailLvl = hslider("Tail [idx:3]",    0.4,  0.0, 1.0,  0.001);
hpFreq  = hslider("HP Freq [idx:4]", 2000, 500, 8000, 1);
hpReso  = hslider("HP Reso [idx:5]", 1.0,  0.5, 10.0, 0.01);
drive   = hslider("Drive [idx:6]",   1.0,  1.0, 20.0, 0.01);
curve   = hslider("Curve [idx:7]",   0,   -50, 50,   1);

// ============================================================================
// Voice
// ============================================================================
// A clap is one noise source modulated by a fast multi-triangle envelope: three
// quick triangle pulses (peaks at 0, Spread, 2*Spread) draw the hand-clap flam,
// then a longer diffuse tail. `ts` is the time (s) since the last note-on.
gateRise = gate > gate';
ts = (+(1.0 / ma.SR) : *(1.0 - gateRise)) ~ _;

w      = spread * 0.5;
tri(c) = max(0.0, 1.0 - abs(ts - c) / w);
bursts = tri(0.0) + tri(spread) + tri(2.0 * spread);
curveExp = pow(8.0, -curve / 50.0);
tail   = pow(en.ar(0.001, decay, gate), curveExp) * tailLvl;

// Band-pass into a resonant high-pass (~2 kHz) for bite, then tanh drive.
nz      = no.noise : fi.resonbp(tone, 1.6, 1.0) : fi.resonhp(hpFreq, hpReso, 1.0);
voice   = ma.tanh(nz * (bursts + tail) * drive) * gain;
process = voice <: _, _;
