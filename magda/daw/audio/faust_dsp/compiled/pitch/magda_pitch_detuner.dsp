declare name "MagdaPitchDetuner";
declare description "Stereo detuner - two ef.transpose voices, +Pitch on L and -Pitch on R, for thick chorus-without-modulation widening.";

import("stdfaust.lib");

// Shared 5-slot DSP surface (idx 1..5) plus the wrapper-only Engine at idx 0.

pitchSemis = hslider("Pitch [unit:st] [idx:1]", 0.1, -24.0, 24.0, 0.01) : si.smooth(ba.tau2pole(0.05));
fineCents  = hslider("Fine [unit:cents] [idx:2]", 12.0, -100.0, 100.0, 0.1) : si.smooth(ba.tau2pole(0.05));
textureMs  = hslider("Texture [unit:ms] [idx:3]", 50.0, 8.0, 200.0, 0.1);
mix        = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outDb      = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// L gets +shift, R gets -shift - anti-symmetric pair gives the classic
// detune image: low setting = subtle thickening, high setting = wide and
// chorus-like.
shift = pitchSemis + fineCents / 100.0;
shiftL =  shift;
shiftR = -shift;

ms2samp(ms) = ms * ma.SR / 1000.0;
windowSamps = ms2samp(textureMs);
xfadeSamps  = windowSamps * 0.5;

shifterL(x) = ef.transpose(windowSamps, xfadeSamps, shiftL, x);
shifterR(x) = ef.transpose(windowSamps, xfadeSamps, shiftR, x);

db2lin(db) = pow(10.0, db / 20.0);
dryWetMix(d, w) = (1.0 - mix) * d + mix * w;

// Each output channel is its own shifted voice. The two delay-line
// accumulators drift independently, which adds character to the stereo
// image on top of the symmetric pitch detune.
process(l, r) = wL, wR
with {
    wL = dryWetMix(l, shifterL(l)) * db2lin(outDb);
    wR = dryWetMix(r, shifterR(r)) * db2lin(outDb);
};
