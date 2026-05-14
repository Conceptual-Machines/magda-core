declare name "MagdaDimensionHaas";
declare description "Haas-effect stereo widener — short fixed delay on one channel produces a psychoacoustic stereo cue.";

import("stdfaust.lib");

amount  = hslider("Amount [idx:1]", 0.5, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.03));
// Rate is inert here but kept in the surface so the Dimension wrapper can
// expose a uniform 6-slot panel across engines. Faust will optimise the
// zone away if it has no effect — that's fine, the wrapper only writes
// to zones that exist.
rateHz  = hslider("Rate [unit:Hz] [idx:2]", 0.5, 0.05, 4.0, 0.01);
width   = hslider("Width [idx:3]", 100.0, 0.0, 200.0, 0.1) / 100.0 : si.smooth(ba.tau2pole(0.05));
mix     = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outDb   = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// Amount maps to delay time in ms, 0..30. The Haas zone (~5..30 ms) is
// the upper end of "perceived as single source"; beyond that it starts
// to sound like a slap echo, which is the wrong character for a widener.
MAX_HAAS_MS = 30.0;
MAX_DELAY = 4096;

ms2samp(ms) = ms * ma.SR / 1000.0;
delayMs = amount * MAX_HAAS_MS;
delaySamples = min(ms2samp(delayMs), MAX_DELAY - 1);

haasDelay(x) = de.fdelay(MAX_DELAY, delaySamples, x);

// Only the right channel is delayed — classic Haas configuration. The
// listener localises sound at the leading (left) channel while the
// trailing channel adds apparent spaciousness.
wetL(l, r) = l;
wetR(l, r) = haasDelay(r);

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
