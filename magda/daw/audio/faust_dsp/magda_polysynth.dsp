declare name "MagdaPolySynth";
declare description "Polyphonic subtractive synth: sawtooth oscillator into a resonant lowpass with an ADSR amp envelope.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls
// ============================================================================
// These three labels are the Faust polyphony convention. The C++ wrapper's
// mydsp_poly voice allocator drives them per voice from note number, velocity
// and note-on/off — they carry NO [idx] annotation, so they are never exposed
// as host macro slots.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls
// ============================================================================
// Pinned to stable [idx:N] slots and harvested by the wrapper. group=false
// polyphony gives each voice its own copy of these zones; the wrapper fans a
// single host value out to every voice each block.
cutoff = hslider("Cutoff [unit:Hz] [idx:0] [scale:log]", 3000, 50, 18000, 1)
         : si.smooth(ba.tau2pole(0.01));
res    = hslider("Resonance [idx:1]", 0.3, 0.0, 0.95, 0.001);
att    = hslider("Attack [unit:s] [idx:2]", 0.005, 0.001, 2.0, 0.001);
rel    = hslider("Release [unit:s] [idx:3]", 0.4, 0.001, 4.0, 0.001);

// ============================================================================
// DSP
// ============================================================================
env   = en.adsr(att, 0.2, 0.7, rel, gate);
voice = os.sawtooth(freq) * env * gain : fi.resonlp(cutoff, res, 1);

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process = voice <: _, _;
