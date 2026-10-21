declare name "MagdaLadder";
declare description "Moog ladder filter: classic 4-pole low-pass with resonance and drive.";
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

// Capped just under 1.0 - the protected normalised-ladder biquad
// (`moog_vcf_2bn`) is stable across the full audible range, but staying
// short of unity prevents the self-oscillation runaway state at extremes.
q = res * 0.99;

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

ladder(x) = x : ve.moog_vcf_2bn(q, cutoff);

process = par(i, 2, drivenIn : ladder);
