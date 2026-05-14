declare name "MagdaReverbPlate";
declare description "Dattorro plate reverb — dense diffusion network for studio-plate ambience.";

import("stdfaust.lib");

re = library("reverbs.lib");

// ============================================================================
// User controls — [idx:N] mirrors the host wrapper's slot layout. Slot 0
// (Engine) lives only in the wrapper, no DSP zone.
// ============================================================================

mix         = hslider("Mix [idx:1]", 0.3, 0.0, 1.0, 0.001)
            : si.smooth(ba.tau2pole(0.02));
predelayMs  = hslider("Predelay [unit:ms] [idx:2]", 20.0, 0.0, 250.0, 0.1)
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

// Decay 0..1 → dattorro decay coefficient, clamped just below 1 (>=1 diverges).
plateDecay = decay * 0.99;

// Canonical Dattorro diffusion coefficients — produce the classic plate sound
// without exposing extra knobs.
INPUT_DIFF_1 = 0.75;
INPUT_DIFF_2 = 0.625;
DECAY_DIFF_1 = 0.70;
DECAY_DIFF_2 = 0.50;

// Native pre_delay = 0 (wrapper handles it). bw = 0.9995 leaves the
// HF response to the Damping slot.
reverbCore = re.dattorro_rev(0, 0.9995, INPUT_DIFF_1, INPUT_DIFF_2,
                             plateDecay, DECAY_DIFF_1, DECAY_DIFF_2,
                             damping);

// Predelay — 250 ms cap at 96 kHz = 24000 samples.
MAX_PREDELAY_SAMPLES = 24000;
predelaySamples = predelayMs * ma.SR / 1000.0;
preDelay = de.fdelay(MAX_PREDELAY_SAMPLES,
                     min(predelaySamples, MAX_PREDELAY_SAMPLES - 1));

// Pre-filter applies to the wet send only; dry passes through dryWetMixer.
preFilter = fi.highpass(2, lowCutHz) : fi.lowpass(2, highCutHz);

// M/S width — 0=mono, 1=stereo as-is, 2=exaggerated.
applyWidth(L, R) = M + S * width, M - S * width
with {
    M = (L + R) * 0.5;
    S = (L - R) * 0.5;
};

sendChain = par(i, 2, preFilter : preDelay) : reverbCore : applyWidth;

db2lin(db) = pow(10.0, db / 20.0);

process = ef.dryWetMixerConstantPower(mix, sendChain)
        : par(i, 2, *(db2lin(outputDb)));
