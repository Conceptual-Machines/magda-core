declare name "MagdaLimiter";
declare description "Lookahead brickwall limiter — Sanfilippo design with peak-holder + tau-smoothed attack/release.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

thresholdDb = hslider("Threshold [unit:dB] [idx:0]", -1.0, -24.0, 0.0, 0.1);
attackMs    = hslider("Attack [unit:ms] [scale:log] [scaleAnchor:1] [idx:1]",
                      1.0, 0.1, 50.0, 0.01);
holdMs      = hslider("Hold [unit:ms] [scale:log] [scaleAnchor:50] [idx:2]",
                      50.0, 1.0, 500.0, 0.1);
releaseMs   = hslider("Release [unit:ms] [scale:log] [scaleAnchor:200] [idx:3]",
                      200.0, 10.0, 2000.0, 1.0);
mix         = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001)
              : si.smooth(ba.tau2pole(0.02));
outputDb    = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
autogain    = nentry("Autogain [idx:6] [style:menu{'Off':0;'On':1}]",
                     0, 0, 1, 1);

// ============================================================================
// DSP
// ============================================================================

// Lookahead is compile-time in the Faust lib (the delay line is allocated
// statically). 5 ms is a standard mastering-style value — long enough to
// catch incoming peaks, short enough that the latency doesn't disturb
// monitoring. Exposing this as a user-runtime knob would require multiple
// compiled DSPs (one per LD value) and is deferred.
LD = 0.005;

attackS  = max(0.0001, attackMs * 0.001);
holdS    = max(0.001,  holdMs * 0.001);
releaseS = max(0.001,  releaseMs * 0.001);

db2lin(db) = pow(10.0, db / 20.0);

// Autogain reinterprets Threshold as a "drive amount" instead of a
// passive ceiling: input gets pushed up by -thresholdDb, the ceiling is
// fixed at 0 dBFS, and the limiter always tries to normalise output to
// 0 dB. With Autogain off, the limiter is a passive brickwall — peaks
// above thresholdDb get pulled down, signal below is untouched.
preGainLin   = db2lin(autogain * (-thresholdDb));
userCeiling  = pow(10.0, thresholdDb / 20.0);
ceilingLin   = autogain * 1.0 + (1.0 - autogain) * userCeiling;

limited(l, r) = (l * preGainLin), (r * preGainLin)
  : co.limiter_lad_stereo(LD, ceilingLin, attackS, holdS, releaseS);

wetL(l, r) = limited(l, r) : _, !;
wetR(l, r) = limited(l, r) : !, _;

channelBlend(dry, wet) = (dry * (1.0 - mix) + wet * mix) * db2lin(outputDb);

process(l, r) = channelBlend(l, wetL(l, r)),
                channelBlend(r, wetR(l, r));
