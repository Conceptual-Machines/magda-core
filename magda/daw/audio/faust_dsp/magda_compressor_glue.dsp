declare name "MagdaCompressorGlue";
declare description "Feed-forward/feedback blend compressor (Brouns FBFF) — vintage glue character with FBFF knob.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================
//
// idx values match the host-side slot layout in the wrapper. Slots that don't
// apply to the Glue engine (Detector, SC HPF, Use Sidechain) are omitted; the
// wrapper writes those zones only when they exist. Glue exposes one extra
// control (FBFF) on top of the shared compressor surface.

thresholdDb = hslider("Threshold [unit:dB] [idx:1]", -18.0, -60.0, 0.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
ratio       = hslider("Ratio [idx:2]", 4.0, 1.0, 20.0, 0.01)
              : si.smooth(ba.tau2pole(0.02));
attackMs    = hslider("Attack [unit:ms] [scale:log] [scaleAnchor:10] [idx:3]",
                      10.0, 0.1, 200.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
releaseMs   = hslider("Release [unit:ms] [scale:log] [scaleAnchor:100] [idx:4]",
                      120.0, 5.0, 1000.0, 1.0)
              : si.smooth(ba.tau2pole(0.02));
kneeDb      = hslider("Knee [unit:dB] [idx:5]", 6.0, 0.0, 24.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
makeupDb    = hslider("Makeup [unit:dB] [idx:6]", 0.0, 0.0, 24.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
mix         = hslider("Mix [idx:7]", 1.0, 0.0, 1.0, 0.001)
              : si.smooth(ba.tau2pole(0.02));
outputDb    = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
link        = hslider("Link [idx:10]", 1.0, 0.0, 1.0, 0.001)
              : si.smooth(ba.tau2pole(0.02));
fbff        = hslider("FBFF [idx:12]", 0.5, 0.0, 1.0, 0.001)
              : si.smooth(ba.tau2pole(0.02));

// ============================================================================
// DSP
// ============================================================================

// Brouns uses `strength` (0..1) instead of ratio. Ratio 4 → strength 0.75,
// ratio 20 → 0.95. Cap at 1 so we never request infinite-ratio limiting.
strength = min(1.0, 1.0 - (1.0 / max(1.0, ratio)));

attackS  = max(0.0001, attackMs * 0.001);
releaseS = max(0.001,  releaseMs * 0.001);

// prePost = 1: detector placed AFTER the gain computer (log domain,
// return-to-threshold). This is what gives Brouns FBFF its musical, "smoothed
// peaks" feel — the right choice for a glue character.
prePost = 1;

// No internal gain-reduction meter — the wrapper computes a display GR from
// threshold/ratio/knee for the curve view, same as for the Clean engine.
meter = _;

db2lin(db) = pow(10.0, db / 20.0);

// Same soft limit as the Clean engine, so engine swaps don't change the
// overall output ceiling behaviour.
softLimit(x) = ma.tanh(x * 0.75) / ma.tanh(0.75);

compress(l, r) = l, r
  : co.FBFFcompressor_N_chan(strength, thresholdDb, attackS, releaseS,
                             kneeDb, prePost, link, fbff, meter, 2);

channelBlend(dry, wet) = (dry * (1.0 - mix) + wet * mix) * db2lin(makeupDb + outputDb)
                         : softLimit;

wetL(l, r) = compress(l, r) : _, !;
wetR(l, r) = compress(l, r) : !, _;

// Third input (sc) is accepted for I/O symmetry with the Clean engine but
// not used — Glue runs on the internal bus only. External sidechain support
// would require swapping to the gain-only blocks; deferred.
process(l, r, sc) = channelBlend(l, wetL(l, r)),
                    channelBlend(r, wetR(l, r));
