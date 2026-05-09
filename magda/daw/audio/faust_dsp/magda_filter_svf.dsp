declare name "MagdaSVF";
declare description "State-Variable Filter — clean LP/BP/HP/Notch, 2-pole, with pre-filter drive saturation.";

import("stdfaust.lib");

cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                0, 0, 3, 1);

// SVF Q maps 0.5 (gentle) .. 12.0. The Faust SVF can become aggressive
// close to Nyquist while sweeping; keep the top of the range musical rather
// than near self-oscillation.
q = 0.5 + res * 11.5;
safeCutoff = min(cutoff, ma.SR * 0.45);

// Drive: dry/saturated lerp; tanh(4) normalisation keeps unity-amplitude
// signals at unity at full drive.
drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// Run all four modes in parallel and pick one. Each mode is a 2-pole
// filter; the four-fold cost is negligible.
svf(x) = ((x : fi.svf.lp(safeCutoff, q)),
          (x : fi.svf.bp(safeCutoff, q)),
          (x : fi.svf.hp(safeCutoff, q)),
          (x : fi.svf.notch(safeCutoff, q))) : ba.selectn(4, int(mode));

// Stereo: independent filter state per channel.
process = par(i, 2, drivenIn : svf);
