declare name "MagdaReverbRoom";
declare description "Freeverb-style room reverb: Schroeder-Moorer comb/allpass network for small-space ambience.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

re = library("reverbs.lib");

// ============================================================================
// User controls - [idx:N] mirrors the host wrapper's slot layout.
// ============================================================================

// Smoothed as the two crossfade gains rather than as the control, so the
// constant-power cos() pair stays at control rate.
mix         = hslider("Mix [idx:1]", 0.3, 0.0, 1.0, 0.001);
predelayMs  = hslider("Predelay [unit:ms] [idx:2]", 10.0, 0.0, 250.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));
decay       = hslider("Decay [idx:3]", 50.0, 0.0, 100.0, 0.1) / 100.0
            : si.smooth(ba.tau2pole(0.05));
damping     = hslider("Damping [idx:4]", 30.0, 0.0, 100.0, 0.1) / 100.0
            : si.smooth(ba.tau2pole(0.05));
// Unsmoothed: each drives a bilinear-transform tan() inside tf2s, and
// everything downstream of it is plain arithmetic. Smoothed that is one
// tan() per filter per sample; raw it is one per block. Decay and damping
// keep their smoothing here - unlike Hall's zita, neither feeds a
// transcendental.
lowCutHz    = hslider("Low Cut [unit:Hz] [scale:log] [scaleAnchor:80] [idx:5]",
                      40.0, 20.0, 500.0, 1.0);
highCutHz   = hslider("High Cut [unit:Hz] [scale:log] [scaleAnchor:8000] [idx:6]",
                      12000.0, 1000.0, 18000.0, 1.0);
width       = hslider("Width [idx:7]", 100.0, 0.0, 200.0, 0.1) / 100.0
            : si.smooth(ba.tau2pole(0.05));
// Smoothed as a linear gain rather than in dB, so pow() stays at control rate.
outputDb    = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1);

// ============================================================================
// DSP
// ============================================================================

// Decay 0..1 → fb1 0.7..0.98. Below 0.7 the tail is too short to be useful;
// 1.0 is unstable (self-oscillating).
fb1 = 0.7 + decay * 0.28;

// Jezar's canonical Freeverb constants. Spread = 23 samples is the stock
// stereo decorrelation offset.
FB2    = 0.5;
SPREAD = 23;

// mono_freeverb sums its eight comb filters with no normalisation, and
// stereo_freeverb sums L+R into each side on top of that. Jezar's original
// compensates with a fixed input gain; the Faust port does not, so the wet
// path arrived roughly 8x hotter than the Plate and Hall engines and clipped
// on its own at any decay setting: a 0.25 amplitude input peaked at 1.50.
//
// 1/8 is the comb count, not a taste value. It puts the wet RMS within a
// hair of Plate across the decay range.
FREEVERB_COMBS = 8.0;

reverbCore = re.stereo_freeverb(fb1, FB2, damping, SPREAD)
           : par(i, 2, /(FREEVERB_COMBS));

MAX_PREDELAY_SAMPLES = 24000;
predelaySamples = predelayMs * ma.SR / 1000.0;
preDelay = de.fdelay(MAX_PREDELAY_SAMPLES,
                     min(predelaySamples, MAX_PREDELAY_SAMPLES - 1));

preFilter = fi.highpass(2, lowCutHz) : fi.lowpass(2, highCutHz);

applyWidth(L, R) = M + S * width, M - S * width
with {
    M = (L + R) * 0.5;
    S = (L - R) * 0.5;
};

sendChain = par(i, 2, preFilter : preDelay) : reverbCore : applyWidth;

db2lin(db) = pow(10.0, db / 20.0);
outputGain = db2lin(outputDb) : si.smooth(ba.tau2pole(0.02));

// ef.dryWetMixerConstantPower's crossfade with the cos() pair evaluated once
// per block and the resulting gains smoothed. The 1/sqrt(2) is that mixer's
// own weighting and does not cancel: fully dry passes 0.707, and dropping it
// would add 3 dB to the dry path of every existing project.
dryWetScale = 0.70710678;
dryGain = cos(mix * ma.PI * 0.5) * dryWetScale : si.smooth(ba.tau2pole(0.02));
wetGain = sin(mix * ma.PI * 0.5) * dryWetScale : si.smooth(ba.tau2pole(0.02));

process = _, _ <: (si.bus(2), sendChain)
        : (par(i, 2, *(dryGain)), par(i, 2, *(wetGain)))
       :> par(i, 2, *(outputGain));
