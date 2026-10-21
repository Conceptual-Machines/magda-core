declare name "MagdaSallenKey";
declare description "Sallen-Key 2nd-order filter: LP/BP/HP, smooth analog character.";
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

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'BP':1;'HP':2}]", 0, 0, 2, 1);

nf = log(cutoff / 20.0) / log(1000.0);
q  = 0.7 * pow(10.0 / 0.7, res);  // Q 0.7..10, exponential

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

sk(x) = ((x : ve.sallenKey2ndOrderLPF(nf, q)),
         (x : ve.sallenKey2ndOrderBPF(nf, q)),
         (x : ve.sallenKey2ndOrderHPF(nf, q))) : ba.selectn(3, int(mode));

process = par(i, 2, drivenIn : sk);
