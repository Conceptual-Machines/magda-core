declare name "MagdaDimensionMS";
declare description "Mid-side stereo widener — scales the side channel directly, no time smear or modulation.";

import("stdfaust.lib");

amount  = hslider("Amount [idx:1]", 0.5, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.03));
rateHz  = hslider("Rate [unit:Hz] [idx:2]", 0.5, 0.05, 4.0, 0.01);  // inert for M/S
width   = hslider("Width [idx:3]", 100.0, 0.0, 200.0, 0.1) / 100.0 : si.smooth(ba.tau2pole(0.05));
mix     = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outDb   = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// Amount maps to side gain: 0 → 0.5× (image partly collapsed), 1 → 2×
// (image roughly doubled in side energy). Combined with the surface
// Width slot (a separate M/S scale) this gives fine control over apparent
// stereo image. Pure mono input stays mono — there's no side to scale.
sideGain = 0.5 + amount * 1.5;

applyWidth(L, R) = M + S * width, M - S * width
with {
    M = (L + R) * 0.5;
    S = (L - R) * 0.5;
};

db2lin(db) = pow(10.0, db / 20.0);
dryWetMix(d, w) = (1.0 - mix) * d + mix * w;

processed(l, r) = applyWidth(L2, R2)
with {
    mid = (l + r) * 0.5;
    side = (l - r) * 0.5;
    L2 = mid + side * sideGain;
    R2 = mid - side * sideGain;
};

process(l, r) = wL, wR
with {
    p = processed(l, r);
    wetL = p : _, !;
    wetR = p : !, _;
    wL = dryWetMix(l, wetL) * db2lin(outDb);
    wR = dryWetMix(r, wetR) * db2lin(outDb);
};
