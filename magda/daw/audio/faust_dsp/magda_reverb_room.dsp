declare name "MagdaReverbRoom";
declare description "Freeverb-style room reverb — Schroeder-Moorer comb/allpass network for small-space ambience.";

import("stdfaust.lib");

re = library("reverbs.lib");

// ============================================================================
// User controls — [idx:N] mirrors the host wrapper's slot layout.
// ============================================================================

mix         = hslider("Mix [idx:1]", 0.3, 0.0, 1.0, 0.001)
            : si.smooth(ba.tau2pole(0.02));
predelayMs  = hslider("Predelay [unit:ms] [idx:2]", 10.0, 0.0, 250.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));
decay       = hslider("Decay [idx:3]", 50.0, 0.0, 100.0, 0.1) / 100.0
            : si.smooth(ba.tau2pole(0.05));
damping     = hslider("Damping [idx:4]", 30.0, 0.0, 100.0, 0.1) / 100.0
            : si.smooth(ba.tau2pole(0.05));
lowCutHz    = hslider("Low Cut [unit:Hz] [scale:log] [scaleAnchor:80] [idx:5]",
                      40.0, 20.0, 500.0, 1.0)
            : si.smooth(ba.tau2pole(0.05));
highCutHz   = hslider("High Cut [unit:Hz] [scale:log] [scaleAnchor:8000] [idx:6]",
                      12000.0, 1000.0, 18000.0, 1.0)
            : si.smooth(ba.tau2pole(0.05));
width       = hslider("Width [idx:7]", 100.0, 0.0, 200.0, 0.1) / 100.0
            : si.smooth(ba.tau2pole(0.05));
outputDb    = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1)
            : si.smooth(ba.tau2pole(0.02));

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

reverbCore = re.stereo_freeverb(fb1, FB2, damping, SPREAD);

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

process = ef.dryWetMixerConstantPower(mix, sendChain)
        : par(i, 2, *(db2lin(outputDb)));
