declare name "MagdaMallet";
declare description "Struck modal-percussion voice (pm.lib): selectable Marimba / Djembe. The per-voice freq/gain/gate are driven by the poly allocator; Strike Pos / Strike Cutoff / Strike Sharpness / Model / Decay are host macros. The chord-latch + curve strum scheduler lives in the shared C++ instrument base (same as Pluck).";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls
// ============================================================================
// Driven per voice by mydsp_poly from note number, velocity and the
// scheduler-issued note-on/off. No [idx] annotation, so never host macro slots.
freq = nentry("freq", 440, 20, 20000, 0.01);
gain = nentry("gain", 0.8, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls
// ============================================================================
// Pinned to stable [idx:N] slots and harvested by the wrapper, fanned to every
// voice each block. Strike Pos is a unified [0,1] control mapped into each
// model's own range; Cutoff is the marimba strike-filter cutoff (djembe derives
// its own); Sharpness is shared; Model picks which pm voice renders; Decay
// scales the intrinsic modal ring of both.
strikePos    = hslider("Strike Pos [idx:0]",       0.5,  0.0,  1.0,    0.001);
strikeCutoff = hslider("Strike Cutoff [idx:1]",    6500, 20.0, 20000,  1.0);
strikeSharp  = hslider("Strike Sharpness [idx:2]", 0.5,  0.01, 5.0,    0.01);
model        = nentry("Model [idx:3]",             0,    0,    1,      1);  // 0=Marimba 1=Djembe
decay        = hslider("Decay [idx:4]",            0.4,  0.0,  1.0,    0.001);

// ============================================================================
// Voices
// ============================================================================
// Both pm models are inlined (rather than calling pm.marimba / pm.djembe) so the
// modal decay - hardcoded in those wrappers - is driven by the Decay macro.
// ba.selectn is strict (both branches run), which is fine for two cheap modal
// voices.

// Marimba: tuned bar + resonator tube. pm.marimba bakes in maxT60 = 0.1 s, a
// very short, quiet ring; we expose it as 0.15 .. 2.0 s. strikePosition is a
// 0..4 bar position, so the unified [0,1] control is scaled up.
marimbaMaxT60 = 0.15 + decay * 1.85;
marimbaVoice = pm.strikeModel(10, strikeCutoff, strikeSharp, gain, gate)
             : pm.marimbaBarModel(freq, strikePos * 4.0, marimbaMaxT60, 1, 5)
             : pm.marimbaResTube(pm.f2l(freq));

// Djembe: membrane drum, 20 empirical modes. pm.djembe bakes in
// modeT60s = (20-i)*0.03 s; we scale that 1x .. 5x. strikePosition is already
// 0..1 and there is no strike cutoff.
djembeScale  = 1.0 + decay * 4.0;
djembeGain(i)  = 1.0 / ((i + 1) * (i + 1));
djembeModel(f) = _ <: par(i, 20, pm.modeFilter(f + 200.0 * i, (20 - i) * 0.03 * djembeScale,
                                               djembeGain(i)))
                 :> /(20);
djembeVoice = pm.strike(strikePos, strikeSharp, gain, gate) : djembeModel(freq);

voice = ba.selectn(2, int(model), marimbaVoice, djembeVoice);

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process = voice <: _, _;
