declare name "MagdaPitchHarmonizer";
declare description "Single-interval harmonizer - one ef.transpose voice summed with dry. Pitch sets the interval; Mix governs the harmony level.";

import("stdfaust.lib");

// Shared 5-slot DSP surface (idx 1..5) plus the wrapper-only Engine at idx 0.

pitchSemis = hslider("Pitch [unit:st] [idx:1]", 7.0, -24.0, 24.0, 0.01) : si.smooth(ba.tau2pole(0.05));
fineCents  = hslider("Fine [unit:cents] [idx:2]", 0.0, -100.0, 100.0, 0.1) : si.smooth(ba.tau2pole(0.05));
textureMs  = hslider("Texture [unit:ms] [idx:3]", 50.0, 8.0, 200.0, 0.1);
mix        = hslider("Mix [idx:4]", 0.5, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outDb      = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// Pitch is the harmonic interval - default to a perfect fifth (7 semis)
// because that's the most recognisable "harmonizer" sound. Mix is the
// dry/(dry+harmony) blend; Mix=0.5 is the classic "you hear both equally"
// setting, hence the default.
shift = pitchSemis + fineCents / 100.0;

ms2samp(ms) = ms * ma.SR / 1000.0;
windowSamps = ms2samp(textureMs);
xfadeSamps  = windowSamps * 0.5;

shifter(x) = ef.transpose(windowSamps, xfadeSamps, shift, x);

db2lin(db) = pow(10.0, db / 20.0);
dryWetMix(d, w) = (1.0 - mix) * d + mix * w;

// Per-channel shift summed with dry. Stereo character comes from the two
// independent delay-line accumulators (one per channel) - same trick the
// Shifter engine uses.
process(l, r) = wL, wR
with {
    wL = dryWetMix(l, shifter(l)) * db2lin(outDb);
    wR = dryWetMix(r, shifter(r)) * db2lin(outDb);
};
