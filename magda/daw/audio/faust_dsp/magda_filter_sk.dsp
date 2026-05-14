declare name "MagdaSallenKey";
declare description "Sallen-Key 2nd-order filter — LP/BP/HP, smooth analog character.";

import("stdfaust.lib");

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'BP':1;'HP':2}]", 0, 0, 2, 1);

nf = log(cutoff / 20.0) / log(1000.0);
q  = 0.7 + res * 9.3;

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

sk(x) = ((x : ve.sallenKey2ndOrderLPF(nf, q)),
         (x : ve.sallenKey2ndOrderBPF(nf, q)),
         (x : ve.sallenKey2ndOrderHPF(nf, q))) : ba.selectn(3, int(mode));

process = par(i, 2, drivenIn : sk);
