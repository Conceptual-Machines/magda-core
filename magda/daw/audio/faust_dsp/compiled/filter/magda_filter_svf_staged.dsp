declare name "MagdaSVFStaged";
declare description "The SVF's LP/BP/HP/Notch with 4 or 6 poles: stages in series.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
msm = library("magda_smoothing.lib");
mf = library("magda_filters.lib");

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
drivenIn = mf.drive(drive);

// Notch+ is listed so every SVF variant reads the mode slot on the same steps.
mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3;'Notch+':4}]", 0, 0, 4, 1);
poles  = nentry("Poles [idx:5] [style:menu{'2':0;'4':1;'6':2}]", 1, 0, 2, 1);

// 2 poles is the plain SVF's job; here it plays 4.
m = int(min(mode, 3));
staged = mf.svfPoles(m, max(1, int(poles)), g, k);

process = par(i, 2, drivenIn : staged);
