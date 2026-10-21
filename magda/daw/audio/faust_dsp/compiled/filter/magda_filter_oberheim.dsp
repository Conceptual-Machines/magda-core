declare name "MagdaOberheim";
declare description "Virtual analog Oberheim SEM filter: LP/BP/HP/Notch from the same shared core.";
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

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                0, 0, 3, 1);

// `ve.oberheim*` take a log-normalised 0..1 control - invert to Hz.
nf = log(cutoff / 20.0) / log(1000.0);
q  = 0.5 * pow(60.0, res);  // Oberheim SEM Q 0.5..30, exponential

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// `ve.oberheim` produces all four modes from one shared filter core
// (BSF, BPF, HPF, LPF - in that output order). Tapping the right one
// is essentially free - the four mode wrappers in vaeffects.lib all
// call the same `oberheim` internally.
oberheim(x) = ((x : ve.oberheimLPF(nf, q)),
               (x : ve.oberheimBPF(nf, q)),
               (x : ve.oberheimHPF(nf, q)),
               (x : ve.oberheimBSF(nf, q))) : ba.selectn(4, int(mode));

process = par(i, 2, drivenIn : oberheim);
