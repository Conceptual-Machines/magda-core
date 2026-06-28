declare name "MagdaMarimba";
declare description "Struck modal marimba: a tuned tone-bar-and-tube physical model (pm.marimbaBarModel + resonator tube) driven by a strike exciter. Follows the played note. The high-level pm.marimba wrapper hardcodes the bar's T60, so this builds the model explicitly to expose a Decay (ring time) knob.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_tom.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]).
// ============================================================================
// Normalised strike position, scaled to the model's 0-4 bar range.
strikePos   = hslider("Strike Position [idx:0]", 0.3, 0.0, 1.0, 0.001);
// Brightness of the strike: cutoff of the exciter's noise burst.
strikeTone  = hslider("Strike Tone [unit:Hz] [idx:1] [scale:log]", 7000, 500, 12000, 1);
// Sharpness of the strike transient (0 = soft mallet, 1 = hard).
strikeSharp = hslider("Strike Sharpness [idx:2]", 0.25, 0.0, 1.0, 0.001);
// Ring time (T60) of the bar in milliseconds (converted to the seconds the model
// wants). The pm.marimba wrapper fixes this at 100 ms; exposing it lets the bar
// ring from a dry knock up to a vibraphone-like sustain. The decay ratio / slope
// keep their model defaults (1 / 5).
decay       = hslider("Decay [unit:ms] [idx:3]", 100, 50, 2000, 1) * 0.001;

// The strike fires on the gate's rising edge (one-sample trigger per note-on).
trigger = gate > gate';

// pm.marimba = strikeModel : marimbaModel, and marimbaModel hardcodes the bar's
// T60 then feeds a resonance tube tuned to the note. Inlined here so Decay drives
// the bar's T60 directly. resTubeLength = speedOfSound / freq (pm.f2l).
voice = pm.strikeModel(10, strikeTone, strikeSharp, gain, trigger)
      : pm.marimbaBarModel(freq, strikePos * 4.0, decay, 1, 5)
      : pm.marimbaResTube(pm.f2l(freq));
process = voice <: _, _;
