declare name "MagdaLadder";
declare description "Moog ladder filter — classic 4-pole low-pass with resonance and drive.";

import("stdfaust.lib");

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// Capped just under 1.0 — the protected normalised-ladder biquad
// (`moog_vcf_2bn`) is stable across the full audible range, but staying
// short of unity prevents the self-oscillation runaway state at extremes.
q = res * 0.99;

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

ladder(x) = x : ve.moog_vcf_2bn(q, cutoff);

process = par(i, 2, drivenIn : ladder);
