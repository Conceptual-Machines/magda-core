declare name "MagdaSVFNotchPlus";
declare description "The SVF's Notch+: a notch at the cutoff between two resonant peaks.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
msm = library("magda_smoothing.lib");
svf = library("magda_svf.lib");

// The plain SVF's controls and coefficients (magda_filter_svf.dsp); see there.
smoothing = hslider("Smoothing [unit:s] [idx:4]", 0.02, 0.0005, 0.1, 0.0001);
ctl = msm.smooth(ba.tau2pole(smoothing));
cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1);
res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001);
drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001) : msm.smooth(ba.tau2pole(0.02));
prewarp(hz) = tan(min(hz, ma.SR * 0.45) * ma.PI / ma.SR) : ctl;
g = prewarp(cutoff);
k = 1.0 / (0.5 * pow(80.0, res)) : ctl;
drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// Two peaks half an octave either side, then a notch at the cutoff. Resonance
// raises and sharpens the peaks; the notch stays at Q 2.
peakK = 1.0 / (0.7 * pow(15.0 / 0.7, res)) : ctl;
peakBoost = 2.0 * res;
notchPlus(x) = x + peakBoost * (lower + upper) : svf.stage(3, g, 0.5, 1.0)
with {
    lower = x : svf.unityBp(prewarp(cutoff * 0.7071), peakK);
    upper = x : svf.unityBp(prewarp(cutoff * 1.4142), peakK);
};

process = par(i, 2, drivenIn : notchPlus);
