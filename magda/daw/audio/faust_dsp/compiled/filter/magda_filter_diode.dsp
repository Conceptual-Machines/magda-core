declare name "MagdaDiode";
declare description "Virtual analog diode ladder filter: resonant 4-pole low-pass with input drive.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
msm = library("magda_smoothing.lib");

// How fast cutoff and resonance follow a change. The Filter device leaves it at
// 20 ms; a sampler voice shortens it so its filter envelope is not smeared (#3097).
smoothing = hslider("Smoothing [unit:s] [idx:4]", 0.02, 0.0005, 0.1, 0.0001);
ctl = msm.smooth(ba.tau2pole(smoothing));

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : ctl;

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : ctl;

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : msm.smooth(ba.tau2pole(0.02));

// ve.diodeLadder expects a 0..1 normalised cutoff and a Q value in the
// example range 0.7..20; exponential across it.
normFreq = min(1.0, max(0.0, cutoff / (ma.SR * 0.5)));
q = 0.7 * pow(20.0 / 0.7, res);

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

diode(x) = x : ve.diodeLadder(normFreq, q);

process = par(i, 2, drivenIn : diode);
