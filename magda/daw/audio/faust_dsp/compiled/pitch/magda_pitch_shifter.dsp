declare name "MagdaPitchShifter";
declare description "Two-delay-line pitch shifter (ef.transpose) - single voice, full +/-24 semitone range. Granular crossfade artefacts are the character.";

import("stdfaust.lib");

// idx 0 is the wrapper-only Engine slot. DSP zones live at idx 1..5 and
// are the same across all three Pitch engines so swapping between them
// preserves the user's settings.

pitchSemis = hslider("Pitch [unit:st] [idx:1]", 0.0, -24.0, 24.0, 0.01) : si.smooth(ba.tau2pole(0.05));
fineCents  = hslider("Fine [unit:cents] [idx:2]", 0.0, -100.0, 100.0, 0.1) : si.smooth(ba.tau2pole(0.05));
textureMs  = hslider("Texture [unit:ms] [idx:3]", 50.0, 8.0, 200.0, 0.1);
mix        = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outDb      = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// Total shift in semitones - Fine is the per-cent trim added to the coarse
// semitone setting.
shift = pitchSemis + fineCents / 100.0;

// Texture controls the window size (and thus crossfade duration). Smaller
// windows give the classic grainy/buzzy shifter sound; larger windows
// smooth out at the cost of perceptible latency on transients.
ms2samp(ms) = ms * ma.SR / 1000.0;
windowSamps = ms2samp(textureMs);
xfadeSamps  = windowSamps * 0.5;

shifter(x) = ef.transpose(windowSamps, xfadeSamps, shift, x);

db2lin(db) = pow(10.0, db / 20.0);
dryWetMix(d, w) = (1.0 - mix) * d + mix * w;

// Independent shifter per channel - the two delay-line accumulators drift
// out of phase, which adds a stereo cue to mono-ish input. Bonus artefact.
process(l, r) = wL, wR
with {
    wL = dryWetMix(l, shifter(l)) * db2lin(outDb);
    wR = dryWetMix(r, shifter(r)) * db2lin(outDb);
};
