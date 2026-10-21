declare name "MagdaKorg35";
declare description "Virtual analog Korg 35 filter: LP and HP variants of the MS-10/MS-20 character.";
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

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'HP':1}]", 0, 0, 1, 1);

// `ve.korg35*` take a 0..1 control that the library remaps internally as
// `freq = 2 * 10^(3*normFreq + 1)` - invert that so the cutoff knob's Hz
// value drives the filter as expected.
nf = log(cutoff / 20.0) / log(1000.0);

// Korg35 Q range 0.7..10 (lib examples cap at 10), exponential.
q = 0.7 * pow(10.0 / 0.7, res);

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

korg35(x) = ((x : ve.korg35LPF(nf, q)),
             (x : ve.korg35HPF(nf, q))) : ba.selectn(2, int(mode));

process = par(i, 2, drivenIn : korg35);
