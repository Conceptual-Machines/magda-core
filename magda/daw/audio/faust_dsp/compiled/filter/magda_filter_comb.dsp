declare name "MagdaComb";
declare description "Comb filter: peaks at the cutoff and its harmonics, resonance as feedback.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
msm = library("magda_smoothing.lib");

smoothing = hslider("Smoothing [unit:s] [idx:4]", 0.02, 0.0005, 0.1, 0.0001);
ctl = msm.smooth(ba.tau2pole(smoothing));

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : ctl;

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : ctl;

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : msm.smooth(ba.tau2pole(0.02));

// + puts the peaks on the cutoff's harmonics; - on its odd half-harmonics, an
// octave down and hollow.
mode   = nentry("Mode [idx:3] [style:menu{'+':0;'-':1}]", 0, 0, 1, 1);

drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// 23 Hz at 192 kHz. A sampler holds one per voice, so the line is kept short.
maxDelay = 8192;
period = max(2.0, min(maxDelay - 4, ma.SR / max(cutoff, 5.0)));
sign = 1.0 - 2.0 * (mode > 0.5);
feedback = 0.98 * res;

// A feedback comb with a feedforward tap: the tap keeps the notches between
// the peaks at zero for any feedback, so resonance 0 is still a comb. The
// sqrt holds the level roughly steady as feedback narrows the peaks.
// The tap is the loop's own delayed signal, so the comb holds one delay line.
comb(x) = x : (loop ~ (de.fdelay3(maxDelay, period - 1.0), !)) : tap
with {
    // The recursion adds one sample, so the loop's own delay is one short.
    loop(delayed, in) = in + sign * feedback * delayed, delayed;
    tap(y, delayed) = 0.5 * sqrt(1.0 - feedback) * (y + sign * delayed);
};

process = par(i, 2, drivenIn : comb);
