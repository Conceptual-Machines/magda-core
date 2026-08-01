declare name "MagdaSVF";
declare description "State-Variable Filter: clean LP/BP/HP/Notch, 2-pole, with pre-filter drive saturation.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

smoo = si.smooth(ba.tau2pole(0.02));

// Cutoff and resonance are deliberately NOT smoothed here. A smoothed
// frequency is a per-sample signal, which forces tan() into the sample loop:
// this filter's single tan() call measured ~16% of its total cost on arm64,
// and considerably more where libm's tanf is slower. Smoothing the derived
// coefficients instead (see g / k below) keeps tan() at control rate.
cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 5, 20000, 1);

res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001);

drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001) : smoo;

mode   = nentry("Mode [idx:3] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                0, 0, 3, 1);

// SVF Q maps 0.5 (gentle) .. 12.0. The Faust SVF can become aggressive
// close to Nyquist while sweeping; keep the top of the range musical rather
// than near self-oscillation.
q = 0.5 + res * 11.5;
safeCutoff = min(cutoff, ma.SR * 0.45);

// The two TPT coefficients, computed once per block and then smoothed at
// audio rate. Smoothing g rather than the frequency traces a marginally
// different path across a sweep, since tan is nonlinear, but both are
// 20 ms one-poles and the endpoints are identical.
g = tan(safeCutoff * ma.PI / ma.SR) : smoo;
k = 1.0 / q : smoo;

// Drive: dry/saturated lerp; tanh(4) normalisation keeps unity-amplitude
// signals at unity at full drive.
drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// fi.svf's topology (Zavalishin TPT, Simper's form) taking the prewarped
// coefficients directly instead of (freq, Q). Identical arithmetic to
// fi.svf.{lp,bp,hp,notch} with A = 1; only the point at which g and k are
// computed differs.
svfCoef(T) = tick ~ (_,_) : !,!,si.dot(3, mix)
with {
    tick(ic1eq, ic2eq, v0) =
        2 * v1 - ic1eq,
        2 * v2 - ic2eq,
        v0, v1, v2
    with {
        v1 = ic1eq + g * (v0 - ic2eq) : /(1 + g * (g + k));
        v2 = ic2eq + g * v1;
    };

    mix = case {
        (0) => 0, 0, 1;
        (1) => 0, 1, 0;
        (2) => 1, -k, -1;
        (3) => 1, -k, 0;
    } (T);
};

// Run all four modes in parallel and pick one. Each mode is a 2-pole
// filter; the four-fold cost is negligible.
svf(x) = ((x : svfCoef(0)),
          (x : svfCoef(1)),
          (x : svfCoef(2)),
          (x : svfCoef(3))) : ba.selectn(4, int(mode));

// Stereo: independent filter state per channel.
process = par(i, 2, drivenIn : svf);
