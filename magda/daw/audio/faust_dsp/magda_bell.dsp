declare name "MagdaBell";
declare description "Struck modal bell: a church-bell physical model (pm.churchBellModel) driven by a strike exciter. Fixed-pitch model with no frequency input, so the played note only triggers it. The high-level pm.churchBell wrapper hardcodes a ~30s ring; this builds the model explicitly to expose a Decay (ring time) knob.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_tom.dsp)
// ============================================================================
// The bell model has no freq input (fixed pitch), so the note only gates the
// voice (like the drum voices). freq is declared so the wrapper has a zone to
// drive, but it is unused here (dead-code-eliminated).
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]).
// ============================================================================
// Normalised strike position, scaled to the model's 0-6 excitation range.
strikePos   = hslider("Strike Position [idx:0]", 0.3, 0.0, 1.0, 0.001);
strikeTone  = hslider("Strike Tone [unit:Hz] [idx:1] [scale:log]", 7000, 500, 12000, 1);
strikeSharp = hslider("Strike Sharpness [idx:2]", 0.25, 0.0, 1.0, 0.001);
// Ring time (T60) in milliseconds (converted to seconds). The pm.churchBell
// wrapper fixes this at 30 s (a full cathedral ring); exposing it lets the bell
// go from a short tine to a long toll. The decay ratio / slope keep the
// wrapper's bell defaults (1 / 2.5).
decay       = hslider("Decay [unit:ms] [idx:3]", 8000, 300, 40000, 1) * 0.001;

trigger = gate > gate';

// pm.churchBell = strikeModel : churchBellModel(50, pos, 30, 1, 2.5). Inlined so
// Decay drives the model's T60 directly.
voice = pm.strikeModel(10, strikeTone, strikeSharp, gain, trigger)
      : pm.churchBellModel(50, strikePos * 6.0, decay, 1, 2.5);
process = voice <: _, _;
