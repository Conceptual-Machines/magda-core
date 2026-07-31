declare name "Vintage Limiter";
declare description "Lookahead brickwall limiter, Sanfilippo design with peak-holder + tau-smoothed attack/release. Runtime effect, distinct from the built-in Limiter device.";
declare license "GPL-3.0";
declare version "1.0";

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
// statically). 5 ms is a standard mastering-style value - long enough to
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
// 0 dB. With Autogain off, the limiter is a passive brickwall - peaks
// above thresholdDb get pulled down, signal below is untouched.
preGainLin   = db2lin(autogain * (-thresholdDb));
userCeiling  = pow(10.0, thresholdDb / 20.0);
ceilingLin   = autogain * 1.0 + (1.0 - autogain) * userCeiling;

limited(l, r) = (l * preGainLin), (r * preGainLin)
  : co.limiter_lad_stereo(LD, ceilingLin, attackS, holdS, releaseS);

wetL(l, r) = limited(l, r) : _, !;
wetR(l, r) = limited(l, r) : !, _;

channelBlend(dry, wet) = (dry * (1.0 - mix) + wet * mix) * db2lin(outputDb);

// ============================================================================
// Gain reduction readout
// ============================================================================

// The ballistics belong here, not in the host. MAGDA polls the bargraph's
// zone about thirty times a second against roughly 1 ms blocks, so it sees
// one block in thirty; a raw instantaneous value would miss every peak in
// between and the meter would look broken. Holding and smoothing in the DSP
// means the host reads a figure that is already true over its whole window.
grEnvDb(x) = x : abs : an.amp_follower_ud(0.001, 0.05)
               : max(ma.EPSILON) : ba.linear2db;

// Positive dB of reduction, so the bar fills as the limiter works. The
// pre-gain is excluded: with Autogain on it is the drive the user asked for,
// not gain the limiter took away.
grDb(l, r) = max(grEnvDb(l * preGainLin) - grEnvDb(wetL(l, r)),
                 grEnvDb(r * preGainLin) - grEnvDb(wetR(l, r)))
             : max(0.0)
             : ba.peakholder(ba.sec2samp(0.25));

// vbargraph rather than hbargraph: a limiter's reduction is read as a level,
// and MAGDA renders a vertical output as a column with the figure below it.
grMeter(l, r) = grDb(l, r)
  : vbargraph("GR [unit:dB] [tooltip:Gain reduction the limiter is applying]",
              0.0, 24.0);

// attach() keeps the meter out of the audio path while still forcing it to be
// computed, which is the standard Faust idiom for a readout that nothing
// downstream consumes (see dm.gate_demo).
process(l, r) = attach(channelBlend(l, wetL(l, r)), grMeter(l, r)),
                channelBlend(r, wetR(l, r));
