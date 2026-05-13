declare name "MagdaDiode";
declare description "Virtual analog diode ladder filter — resonant 4-pole low-pass with input drive.";

import("stdfaust.lib");

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// ve.diodeLadder expects a 0..1 normalised cutoff and a Q value in the
// example range 0.7..20. Keep the top below the demo max so resonance is
// musical under modulation rather than constantly near runaway.
normFreq = min(1.0, max(0.0, cutoff / (ma.SR * 0.5)));
q = 0.7 + res * 15.3;

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

diode(x) = x : ve.diodeLadder(normFreq, q);

process = par(i, 2, drivenIn : diode);
