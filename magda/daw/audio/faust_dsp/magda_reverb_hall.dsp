declare name "MagdaReverbHall";
declare description "Zita FDN hall reverb — 8-tap feedback delay network for smooth large-space tails.";

import("stdfaust.lib");

re = library("reverbs.lib");

// ============================================================================
// User controls — [idx:N] mirrors the host wrapper's slot layout.
// ============================================================================

mix         = hslider("Mix [idx:1]", 0.3, 0.0, 1.0, 0.001)
            : si.smooth(ba.tau2pole(0.02));
predelayMs  = hslider("Predelay [unit:ms] [idx:2]", 20.0, 0.0, 250.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));
decay       = hslider("Decay [idx:3]", 50.0, 0.0, 100.0, 0.1)
            : si.smooth(ba.tau2pole(0.1));
damping     = hslider("Damping [idx:4]", 30.0, 0.0, 100.0, 0.1)
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

// Decay 0..100 → 1.0..15.0 s mid-band T60. The min keeps zita's internal
// `|g|<1` filter constraint comfortably satisfied; smoothing happens on
// the slider so the t60 itself is never zero.
t60m  = 1.0 + decay * 0.14;
// Low-band decay slightly longer for warmth.
t60dc = t60m * 1.2;

// Damping 0..100 → mid/high crossover 14k..1k Hz (more damping = lower f2,
// faster HF rolloff in the tail).
f2 = 14000.0 - damping * 130.0;
// Low/mid crossover — fixed at zita-rev1 default.
F1 = 200.0;

// fsmax must be a literal compile-time constant — allocates the worst-case
// internal delay lines. 96 kHz covers all supported MAGDA sample rates.
FSMAX = 96000;

reverbCore = re.zita_rev1_stereo(0, F1, f2, t60dc, t60m, FSMAX);

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
