declare name "MagdaCompressorGlue";
declare description "Brouns FBFF compressor with exposed character controls (Detector, Style, FBFF).";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================
//
// idx values match the host-side slot layout in the wrapper. SC HPF (idx 11)
// and SC input are not used by Glue (the Brouns block has no external SC
// hook). All character-affecting knobs are user-exposed.
//
// NOTE on smoothing: the Brouns gain blocks already smooth via internal
// onePoleSwitching(att, rel). Wrapping the slider in `si.smooth(...)` here
// would blind Faust's interval analysis for the slidingRMS buffer inside
// RMS_FBFFcompressor (the IIR's output isn't statically bounded, so
// sec2samp(rel)*max(1) is inferred as INT_MAX, which the buffer allocator
// rejects). Pass the raw slider through — the lib smooths it internally.

thresholdDb = hslider("Threshold [unit:dB] [idx:1]", -18.0, -60.0, 0.0, 0.1);
ratio       = hslider("Ratio [idx:2]", 4.0, 1.0, 20.0, 0.01);
attackMs    = hslider("Attack [unit:ms] [scale:log] [scaleAnchor:10] [idx:3]",
                      10.0, 0.1, 200.0, 0.1);
releaseMs   = hslider("Release [unit:ms] [scale:log] [scaleAnchor:100] [idx:4]",
                      120.0, 5.0, 1000.0, 1.0);
kneeDb      = hslider("Knee [unit:dB] [idx:5]", 6.0, 0.0, 24.0, 0.1);
makeupDb    = hslider("Makeup [unit:dB] [idx:6]", 0.0, 0.0, 24.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
mix         = hslider("Mix [idx:7]", 1.0, 0.0, 1.0, 0.001)
              : si.smooth(ba.tau2pole(0.02));
outputDb    = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
detector    = nentry("Detector [idx:9] [style:menu{'Peak':0;'RMS':1}]",
                     0, 0, 1, 1);
link        = hslider("Link [idx:10]", 1.0, 0.0, 1.0, 0.001);
fbff        = hslider("FBFF [idx:12]", 0.5, 0.0, 1.0, 0.001);
style       = nentry("Style [idx:13] [style:menu{'Pre':0;'Post':1}]",
                     0, 0, 1, 1);
autogain    = nentry("Autogain [idx:14] [style:menu{'Off':0;'On':1}]",
                     0, 0, 1, 1);

// ============================================================================
// DSP
// ============================================================================

// Brouns uses `strength` (0..1). ratio→strength: 1 - 1/ratio.
strength = min(1.0, 1.0 - (1.0 / max(1.0, ratio)));

attackS  = max(0.0001, attackMs * 0.001);
releaseS = max(0.001,  releaseMs * 0.001);

// prePost: 0 = Pre (detector before gain computer, linear return-to-zero,
// sharper); 1 = Post (after, log return-to-threshold, smoother).
prePost = int(style);

meter = _;

db2lin(db) = pow(10.0, db / 20.0);
softLimit(x) = ma.tanh(x * 0.75) / ma.tanh(0.75);

// Both detector topologies run in parallel; the detector slot picks which
// pair of outputs is taken. Faust evaluates both branches every block; cost
// is ~2× a single FBFF block when Glue is active.
compPeak(l, r) = l, r : co.FBFFcompressor_N_chan(strength, thresholdDb,
                                                 attackS, releaseS, kneeDb,
                                                 prePost, link, fbff, meter, 2);
compRMS(l, r)  = l, r : co.RMS_FBFFcompressor_N_chan(strength, thresholdDb,
                                                     attackS, releaseS, kneeDb,
                                                     prePost, link, fbff, meter, 2);

peakL(l, r) = compPeak(l, r) : _, !;
peakR(l, r) = compPeak(l, r) : !, _;
rmsL(l, r)  = compRMS(l, r) : _, !;
rmsR(l, r)  = compRMS(l, r) : !, _;

// Explicit linear crossfade between Peak and RMS paths. detector ∈ {0,1}
// from the menu; the arithmetic stays continuous if anything ever writes a
// fractional value.
mixL(l, r) = (1.0 - detector) * peakL(l, r) + detector * rmsL(l, r);
mixR(l, r) = (1.0 - detector) * peakR(l, r) + detector * rmsR(l, r);

compress(l, r) = mixL(l, r), mixR(l, r);

// Autogain compensates for compression-induced loss assuming peak signal
// at 0 dBFS. Brouns' strength already encodes (1 - 1/ratio); reusing it
// keeps the formula consistent with the FBFF/RMS gain calculators.
autogainDb = autogain * (-(thresholdDb * strength));

// Soft-limit is a safety ceiling; Output gain comes after so the
// user-facing Output knob is uncapped (see magda_compressor.dsp for the
// fix rationale).
channelBlend(dry, wet) =
    softLimit((dry * (1.0 - mix) + wet * mix) * db2lin(makeupDb + autogainDb))
    * db2lin(outputDb);

wetL(l, r) = compress(l, r) : _, !;
wetR(l, r) = compress(l, r) : !, _;

// Third input (sc) accepted but unused — Brouns block has no SC hook.
process(l, r, sc) = channelBlend(l, wetL(l, r)),
                    channelBlend(r, wetR(l, r));
