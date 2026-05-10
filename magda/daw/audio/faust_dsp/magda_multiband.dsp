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

// Master compression amount. One knob drives both threshold AND ratio for
// every per-band stage — turn it up for "everything compressed", down for
// "barely doing anything".
depth = hslider("Depth [idx:2]", 0.5, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));

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

// Wet/dry blend. 1 = fully compressed, 0 = pure dry passthrough. Sits
// between the band sum and the output trim so makeup gains are wet-only
// and Mix lets the user dial in classic parallel compression.
mix = hslider("Mix [idx:7]", 1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));

// Final output trim (after the wet/dry blend).
outGainDb = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1)
            : si.smooth(ba.tau2pole(0.05));

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

// Depth maps to threshold and ratio together so a single knob walks the
// "no comp" → "fully OTT" axis. Numbers picked to feel similar to OTT's
// own depth control:
//   depth = 0    → threshold = -3 dB,  ratio = 1.2 (almost transparent)
//   depth = 1    → threshold = -36 dB, ratio = 8.0 (heavy crush)
threshDb = -3.0 - 33.0 * depth;
ratio = 1.2 + 6.8 * depth;

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
downGainDb(levelDb) = max(0.0, levelDb - threshDb) * (1.0 / ratio - 1.0);
upGainDb(levelDb) = max(0.0, threshDb - levelDb) * (1.0 - 1.0 / ratio);
combinedGainDb(levelDb) = downGainDb(levelDb) + upGainDb(levelDb);

// OTT-style per-band stage: feed-forward gain modulation. The detector
// sees the band's own signal, the gain control is applied right back to
// it. No upward-compression-only-when-quiet gating — combinedGainDb is
// continuous.
ottBand(x) = x * (envFollow(x) : ba.linear2db : combinedGainDb : db2lin);

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
wet(x) = x : band3split : par(i, 3, ottBand) : *(db2lin(lowGainDb)),
         *(db2lin(midGainDb)), *(db2lin(highGainDb)) :> _;

// Per-channel pipeline: blend dry and wet, apply output trim.
channel(x) = ((1.0 - mix) * x + mix * wet(x)) * db2lin(outGainDb);

// Stereo: process L and R independently. Detector decisions are per-channel
// (not stereo-linked) — fine for v1, easy to upgrade later.
process = par(i, 2, channel);
