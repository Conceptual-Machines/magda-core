declare name "MagdaDimensionDim";
declare description "Roland Dimension D-style stereo widener — anti-phase LFO modulated delay lines with cross-channel mixing.";

import("stdfaust.lib");

// idx 0 is the wrapper-only Engine slot. DSP zones live at idx 1..5 and
// are the same across all three Dimension engines so swapping between them
// preserves the user's settings.

amount  = hslider("Amount [idx:1]", 0.5, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.03));
rateHz  = hslider("Rate [unit:Hz] [idx:2]", 0.5, 0.05, 4.0, 0.01) : si.smooth(ba.tau2pole(0.05));
width   = hslider("Width [idx:3]", 100.0, 0.0, 200.0, 0.1) / 100.0 : si.smooth(ba.tau2pole(0.05));
mix     = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outDb   = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// Base delay sits in the Haas zone (~5..15 ms) — long enough for a stereo
// image cue, short enough that the modulation reads as a gentle chorus
// rather than an audible echo. Depth scales with Amount.
baseMs = 8.0;
maxDepthMs = 4.0;
depthMs = amount * maxDepthMs;

// 192 kHz × 12 ms ≈ 2304 samples; round up for headroom.
MAX_DELAY = 4096;

ms2samp(ms) = ms * ma.SR / 1000.0;

lfo = os.osc(rateHz);
delayLms = baseMs + lfo * depthMs;
delayRms = baseMs - lfo * depthMs;  // anti-phase — this is what makes the image breathe

modulatedL(x) = de.fdelay(MAX_DELAY, min(ms2samp(delayLms), MAX_DELAY - 1), x);
modulatedR(x) = de.fdelay(MAX_DELAY, min(ms2samp(delayRms), MAX_DELAY - 1), x);

// Cross-channel feed: each output is its own modulated tap plus a touch
// of the opposite channel's modulated tap. The cross-feed is what lifts
// mono content into a wide stereo image — without it this is just chorus.
crossGain = 0.5 * amount;

wetL(l, r) = modulatedL(l) + crossGain * modulatedR(r);
wetR(l, r) = modulatedR(r) + crossGain * modulatedL(l);

applyWidth(L, R) = M + S * width, M - S * width
with {
    M = (L + R) * 0.5;
    S = (L - R) * 0.5;
};

db2lin(db) = pow(10.0, db / 20.0);

dryWetMix(d, w) = (1.0 - mix) * d + mix * w;

process(l, r) = wL, wR
with {
    rawL = wetL(l, r);
    rawR = wetR(l, r);
    widened = applyWidth(rawL, rawR);
    wetLw = widened : _, !;
    wetRw = widened : !, _;
    wL = dryWetMix(l, wetLw) * db2lin(outDb);
    wR = dryWetMix(r, wetRw) * db2lin(outDb);
};
