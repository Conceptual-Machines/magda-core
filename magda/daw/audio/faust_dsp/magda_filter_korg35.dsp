declare name "MagdaKorg35";
declare description "Virtual analog Korg 35 filter — LP and HP variants of the MS-10/MS-20 character.";

import("stdfaust.lib");

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'HP':1}]", 0, 0, 1, 1);

// `ve.korg35*` take a 0..1 control that the library remaps internally as
// `freq = 2 * 10^(3*normFreq + 1)` — invert that so the cutoff knob's Hz
// value drives the filter as expected.
nf = log(cutoff / 20.0) / log(1000.0);

// Korg35 Q range 0.7..10 (lib examples cap at 10).
q = 0.7 + res * 9.3;

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

korg35(x) = ((x : ve.korg35LPF(nf, q)),
             (x : ve.korg35HPF(nf, q))) : ba.selectn(2, int(mode));

process = par(i, 2, drivenIn : korg35);
