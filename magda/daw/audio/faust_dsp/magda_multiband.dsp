declare name "MagdaMultiband";
declare description "OTT-style 3-band compressor: Linkwitz-Riley splits, parallel upward + downward compression per band, per-band gain.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Low / mid crossover (Hz). Defaults around the classic OTT split points.
xoLow = hslider("Low XO [unit:Hz] [scale:log] [scaleAnchor:200] [idx:0]", 120, 40, 500, 1)
        : si.smooth(ba.tau2pole(0.05));

// Mid / high crossover (Hz). Constrained > xoLow at the host level so the
// LR4 cascade stays well-behaved.
xoHigh = hslider("High XO [unit:Hz] [scale:log] [scaleAnchor:2000] [idx:1]", 2500, 500, 8000, 1)
         : si.smooth(ba.tau2pole(0.05));

// Master compression amount. Per-band thresholds/ratios define the actual
// curves; Depth scales the resulting up/down gain change so it can still act
// as a global "less/more OTT" macro.
depth = hslider("Depth [idx:2]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));

// Master attack/release scaling. 0 = snappy (3 ms attack), 1 = slow & smooth
// (80 ms attack, near-1 s release). Same envelope across all bands.
time = hslider("Time [idx:3]", 0.4, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));

// Per-band post-compression makeup. Lets the user re-balance the spectrum
// after the dynamics stage so heavier compression doesn't sound dull.
lowGainDb = hslider("Low Gain [unit:dB] [idx:4]", 0.0, -24.0, 24.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));
midGainDb = hslider("Mid Gain [unit:dB] [idx:5]", 0.0, -24.0, 24.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));
highGainDb = hslider("High Gain [unit:dB] [idx:6]", 0.0, -24.0, 24.0, 0.1)
             : si.smooth(ba.tau2pole(0.05));

// Wet/dry blend. 1 = fully compressed, 0 = uncompressed crossover-summed
// signal. The dry side intentionally passes through the same splitter as
// the wet side, otherwise partial Mix settings comb-filter against the
// crossover phase response.
mix = hslider("Mix [idx:7]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));

// Final output trim (after the wet/dry blend).
outGainDb = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));

// Per-band dynamics. "Threshold Above" starts downward compression when the
// detector is louder than the threshold. "Threshold Below" starts upward
// compression when the detector is quieter than the threshold. Keeping the
// below threshold lower than the above threshold leaves a neutral window
// between the two curves.
lowThreshAboveDb = hslider("Low Thresh Above [unit:dB] [idx:9]",
                           -24.0, -60.0, 0.0, 0.1)
                   : si.smooth(ba.tau2pole(0.05));
lowThreshBelowDb = hslider("Low Thresh Below [unit:dB] [idx:10]",
                           -48.0, -80.0, 0.0, 0.1)
                   : si.smooth(ba.tau2pole(0.05));
lowRatio = hslider("Low Ratio [idx:11]", 4.0, 1.0, 20.0, 0.01)
           : si.smooth(ba.tau2pole(0.05));

midThreshAboveDb = hslider("Mid Thresh Above [unit:dB] [idx:12]",
                           -24.0, -60.0, 0.0, 0.1)
                   : si.smooth(ba.tau2pole(0.05));
midThreshBelowDb = hslider("Mid Thresh Below [unit:dB] [idx:13]",
                           -48.0, -80.0, 0.0, 0.1)
                   : si.smooth(ba.tau2pole(0.05));
midRatio = hslider("Mid Ratio [idx:14]", 4.0, 1.0, 20.0, 0.01)
           : si.smooth(ba.tau2pole(0.05));

highThreshAboveDb = hslider("High Thresh Above [unit:dB] [idx:15]",
                            -24.0, -60.0, 0.0, 0.1)
                    : si.smooth(ba.tau2pole(0.05));
highThreshBelowDb = hslider("High Thresh Below [unit:dB] [idx:16]",
                            -48.0, -80.0, 0.0, 0.1)
                    : si.smooth(ba.tau2pole(0.05));
highRatio = hslider("High Ratio [idx:17]", 4.0, 1.0, 20.0, 0.01)
            : si.smooth(ba.tau2pole(0.05));

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

// Time maps to attack/release in seconds.
//   time = 0 → attack 3 ms, release 30 ms (transient-safe)
//   time = 1 → attack 80 ms, release ~800 ms (vibe / glue)
attS = 0.003 + 0.077 * time;
relS = 0.030 + 0.770 * time;

// Peak detector with asymmetric smoothing. si.lag_ud takes attack/release
// time constants in seconds — internally converts to one-pole coefficients.
envFollow(x) = abs(x) : si.lag_ud(attS, relS);

// Static curve (in dB) for the parallel up + down compression. Both halves
// share the same threshold and ratio; they sum because they're on opposite
// sides of the threshold.
//   levelDb > threshDb  → downGainDb is negative (attenuation)
//   levelDb < threshDb  → upGainDb is positive  (boost)
//   levelDb = threshDb  → both terms are zero (continuous at the knee)
downGainDb(threshAboveDb, ratio_, levelDb) =
    max(0.0, levelDb - threshAboveDb) * (1.0 / ratio_ - 1.0);
upGainDb(threshBelowDb, ratio_, levelDb) =
    max(0.0, threshBelowDb - levelDb) * (1.0 - 1.0 / ratio_);
combinedGainDb(threshAboveDb, threshBelowDb, ratio_, levelDb) =
    (downGainDb(threshAboveDb, ratio_, levelDb)
     + upGainDb(threshBelowDb, ratio_, levelDb)) * depth;

// OTT-style per-band stage: feed-forward gain modulation. The detector
// sees the band's own signal, the gain control is applied right back to
// it. No upward-compression-only-when-quiet gating — combinedGainDb is
// continuous.
ottBand(threshAboveDb, threshBelowDb, ratio_, x) =
    x * (envFollow(x) : ba.linear2db
         : combinedGainDb(threshAboveDb, threshBelowDb, max(1.0, ratio_))
         : db2lin);

// Linkwitz-Riley 4th-order: two cascaded 2nd-order Butterworth sections.
// Summing the LP and HP outputs is approximately bit-flat in magnitude
// (the small residual phase ripple is the trade for using a clean two-stage
// crossover instead of an allpass-corrected one).
lp_lr4(fc) = fi.lowpass(2, fc) : fi.lowpass(2, fc);
hp_lr4(fc) = fi.highpass(2, fc) : fi.highpass(2, fc);

// 1-in 3-out 3-band split. Stage 1 separates low from (mid+high); stage 2
// then splits the high side into mid and high.
band3split = _ <: lp_lr4(xoLow), (hp_lr4(xoLow) <: lp_lr4(xoHigh), hp_lr4(xoHigh));

// Per-channel wet path: split → compress each band → makeup → sum.
wet(x) = x : band3split
       : (ottBand(lowThreshAboveDb, lowThreshBelowDb, lowRatio),
          ottBand(midThreshAboveDb, midThreshBelowDb, midRatio),
          ottBand(highThreshAboveDb, highThreshBelowDb, highRatio))
       : *(db2lin(lowGainDb)), *(db2lin(midGainDb)), *(db2lin(highGainDb))
       :> _;

// Phase-matched dry path for parallel blend. A raw passthrough dry signal
// does not share the crossover phase response, so partial Mix settings
// produce audible combing. Recombining the unprocessed bands keeps dry and
// wet aligned.
drySplit(x) = x : band3split :> _;

// Per-channel pipeline: blend phase-matched dry and wet, apply output trim.
channel(x) = ((1.0 - mix) * drySplit(x) + mix * wet(x)) * db2lin(outGainDb);

// Stereo: process L and R independently. Detector decisions are per-channel
// (not stereo-linked) — fine for v1, easy to upgrade later.
process = par(i, 2, channel);
