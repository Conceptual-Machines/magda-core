declare name "MagdaDjembe";
declare description "Struck modal djembe: a hand-drum membrane physical model driven by a strike exciter. Follows the played note. The pm.djembe model computes its mode frequencies as freq + spacing*i, so this exposes Decay (ring time), mode Spacing and Inharmonicity in addition to the strike controls.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_tom.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]). The djembe model takes a strike position (0 =
// centre of the membrane, 1 = edge) and sharpness, but no cutoff, so there is no
// Tone control. The modal bank is reimplemented from pm.djembeModel so its
// hardcoded decay / mode-spacing become knobs.
// ============================================================================
strikePos   = hslider("Strike Position [idx:0]", 0.4, 0.0, 1.0, 0.001);
strikeSharp = hslider("Strike Sharpness [idx:1]", 0.5, 0.0, 1.0, 0.001);
// Ring time of the lowest mode in milliseconds (converted to seconds; higher
// modes decay proportionally faster, as in the original model). pm.djembeModel
// fixes this at ~600 ms.
decay       = hslider("Decay [unit:ms] [idx:2]", 600, 50, 3000, 1) * 0.001;
// Frequency spacing between successive modes (Hz). pm.djembeModel fixes this at
// 200; lower clusters the modes near the fundamental (pitched / tom-like),
// higher spreads them out (metallic / gong-like).
spacing     = hslider("Spacing [unit:Hz] [idx:3]", 200, 20, 600, 1);
// Inharmonicity: bends the otherwise-linear mode series, widening the spacing of
// the upper modes (0 = even spacing, as in the original model).
inharm      = hslider("Inharmonicity [idx:4]", 0.0, 0.0, 1.0, 0.001);

trigger = gate > gate';

// Reimplementation of pm.djembeModel with the decay / spacing / inharmonicity
// hardcodes lifted to parameters. modeGains uses theta = 0 (cos(0) = 1), matching
// the library default, so each mode's gain is 1/(i+1)^2.
nModes = 20;
modeFreqs(i) = freq + spacing * i * (1.0 + inharm * i * 0.1);
modeT60s(i)  = (nModes - i) / float(nModes) * decay;
modeGains(i) = 1.0 / float(i + 1) * (1.0 / (i + 1));

voice = pm.strike(strikePos, strikeSharp, gain, trigger)
      : (_ <: par(i, nModes, pm.modeFilter(modeFreqs(i), modeT60s(i), modeGains(i))) :> /(nModes));
process = voice <: _, _;
