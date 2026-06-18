declare name "MagdaPluck";
declare description "Karplus-Strong plucked-string voice (pm.lib). The per-voice freq/gain/gate are driven by the poly allocator; Damping/Pluck Pos/Brightness/Drive are host macros. The curve-shaped strum scheduler lives in the C++ device, which keys this voice on at computed onsets.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls
// ============================================================================
// The Faust polyphony convention: mydsp_poly drives these per voice from note
// number, velocity and the (scheduler-issued) note-on/off. No [idx] annotation,
// so they are never exposed as host macro slots.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls
// ============================================================================
// Pinned to stable [idx:N] slots and harvested by the wrapper. group=false
// polyphony gives each voice its own copy of these zones; the wrapper fans a
// single host value out to every voice each block.
damping  = hslider("Damping [idx:0]",    0.7,  0.0,  1.0,  0.001);
pluckPos = hslider("Pluck Pos [idx:1]",  0.35, 0.02, 0.98, 0.001);
bright   = hslider("Brightness [idx:2]", 0.4,  0.0,  1.0,  0.001)
           : si.smooth(ba.tau2pole(0.01));
drive    = hslider("Drive [idx:3]",      0.0,  0.0,  1.0,  0.001);

// ============================================================================
// Voice
// ============================================================================
// pm.elecGuitar is a complete single-string Karplus-Strong model: excitation +
// tuned waveguide + bridge/nuts. Length is derived from the played frequency;
// the `mute` coefficient (1 = long ring, 0 = instant mute) carries Damping.
length = pm.f2l(freq);
// Damping -> loop loss, mapped exponentially so the control is perceptually
// even. pm.elecGuitar's `mute` relates to decay time exponentially (the action
// otherwise all crowds near mute ~ 1); this mirrors the spike's pow() decay
// map. loss spans ~0.0003 (long sustain) .. ~0.9 (pizzicato) as damping 0->1.
loss = 0.0003 * pow(3000.0, damping);
mute = 1.0 - min(0.97, loss);
str  = pm.elecGuitar(length, pluckPos, mute, gain, gate);

// Brightness: a post one-pole lowpass, warm (~800 Hz) -> bright (~18 kHz) on a
// log map. (The spike's in-loop loop-lowpass is deferred; pm's loop is closed.)
fc       = 800.0 * pow(22.5, bright);
brightLP = fi.lowpass(1, fc);

// Drive: out-of-loop tanh saturation. Unity slope at the origin -> only loud
// transients compress. (The spike's in-loop drive is deferred for the same
// reason as Brightness.)
sat(x)   = ma.tanh(x * (1.0 + drive * 9.0)) / (1.0 + drive * 9.0);

voice    = str : brightLP : sat;

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process  = voice <: _, _;
