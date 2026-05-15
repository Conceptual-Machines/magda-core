declare name "MagdaMultiband";
declare description "OTT-style 3-band compressor: LR4 splits, parallel upward + downward compression per band, per-band expander, two stages in series.";

import("stdfaust.lib");

// ============================================================================
// User controls
// Slots 0-8: knob-only (not editable in the curve editor).
// Slots 9-26: per-band controls edited directly on the curve view.
// Slots 27-28: crossover frequencies, editor-only (hidden from knob grid).
// ============================================================================

// Master depth / time for both stages.
depth  = hslider("Depth [idx:0]",   1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
time   = hslider("Time [idx:1]",    0.4, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
depth2 = hslider("Depth 2 [idx:2]", 0.3, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
time2  = hslider("Time 2 [idx:3]",  0.2, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));

// Per-band post-compression makeup.
lowGainDb  = hslider("Low Gain [unit:dB] [idx:4]",  0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));
midGainDb  = hslider("Mid Gain [unit:dB] [idx:5]",  0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));
highGainDb = hslider("High Gain [unit:dB] [idx:6]", 0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));

mix       = hslider("Mix [idx:7]",              1.0,  0.0,  1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
outGainDb = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.05));

// Per-band thresholds, ratios, and expander (all edited on the curve view).
lowThreshAboveDb  = hslider("Low Thresh Above [unit:dB] [idx:9]",    -24.0, -60.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
lowThreshBelowDb  = hslider("Low Thresh Below [unit:dB] [idx:10]",   -48.0, -80.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
lowRatioAbove     = hslider("Low Ratio Above [idx:11]",                8.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));
lowRatioBelow     = hslider("Low Ratio Below [idx:12]",                8.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));
lowThreshExpandDb = hslider("Low Thresh Expand [unit:dB] [idx:13]",  -72.0, -80.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
lowExpandRatio    = hslider("Low Expand Ratio [idx:14]",               1.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));

midThreshAboveDb  = hslider("Mid Thresh Above [unit:dB] [idx:15]",   -24.0, -60.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
midThreshBelowDb  = hslider("Mid Thresh Below [unit:dB] [idx:16]",   -48.0, -80.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
midRatioAbove     = hslider("Mid Ratio Above [idx:17]",                8.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));
midRatioBelow     = hslider("Mid Ratio Below [idx:18]",                8.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));
midThreshExpandDb = hslider("Mid Thresh Expand [unit:dB] [idx:19]",  -72.0, -80.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
midExpandRatio    = hslider("Mid Expand Ratio [idx:20]",               1.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));

highThreshAboveDb  = hslider("High Thresh Above [unit:dB] [idx:21]", -24.0, -60.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
highThreshBelowDb  = hslider("High Thresh Below [unit:dB] [idx:22]", -48.0, -80.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
highRatioAbove     = hslider("High Ratio Above [idx:23]",              8.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));
highRatioBelow     = hslider("High Ratio Below [idx:24]",              8.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));
highThreshExpandDb = hslider("High Thresh Expand [unit:dB] [idx:25]",-72.0, -80.0,   0.0, 0.1) : si.smooth(ba.tau2pole(0.05));
highExpandRatio    = hslider("High Expand Ratio [idx:26]",             1.0,   1.0,  20.0, 0.01) : si.smooth(ba.tau2pole(0.05));

// Crossover frequencies -- editor-only, hidden from the knob grid.
xoLow  = hslider("Low XO [unit:Hz] [scale:log] [scaleAnchor:200] [idx:27]", 120, 40, 500, 1)
         : si.smooth(ba.tau2pole(0.05));
xoHigh = hslider("High XO [unit:Hz] [scale:log] [scaleAnchor:2000] [idx:28]", 2500, 500, 8000, 1)
         : si.smooth(ba.tau2pole(0.05));

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

attS(t) = 0.003 + 0.077 * t;
relS(t) = 0.030 + 0.770 * t;

envFollow(t, x) = abs(x) : si.lag_ud(attS(t), relS(t));

downGainDb(threshAboveDb, ratioAbove_, levelDb) =
    max(0.0, levelDb - threshAboveDb) * (1.0 / max(1.0, ratioAbove_) - 1.0);

upGainDb(threshBelowDb, ratioBelow_, levelDb) =
    max(0.0, threshBelowDb - levelDb) * (1.0 - 1.0 / max(1.0, ratioBelow_));

// Expander: negative gain (attenuation) below threshExpandDb.
expandGainDb(threshExpandDb, expandRatio_, levelDb) =
    max(0.0, threshExpandDb - levelDb) * (1.0 / max(1.0, expandRatio_) - 1.0);

combinedGainDb(threshAboveDb, threshBelowDb, threshExpandDb,
               ratioAbove_, ratioBelow_, expandRatio_, d, levelDb) =
    (downGainDb(threshAboveDb, ratioAbove_, levelDb)
     + upGainDb(threshBelowDb, ratioBelow_, levelDb)
     + expandGainDb(threshExpandDb, expandRatio_, levelDb)) * d;

ottBand(threshAboveDb, threshBelowDb, threshExpandDb,
        ratioAbove_, ratioBelow_, expandRatio_, d, t, x) =
    x * (envFollow(t, x)
         : ba.linear2db
         : combinedGainDb(threshAboveDb, threshBelowDb, threshExpandDb,
                          ratioAbove_, ratioBelow_, expandRatio_, d)
         : db2lin);

lp_lr4(fc) = fi.lowpass(2, fc) : fi.lowpass(2, fc);
hp_lr4(fc) = fi.highpass(2, fc) : fi.highpass(2, fc);
band3split  = _ <: lp_lr4(xoLow), (hp_lr4(xoLow) <: lp_lr4(xoHigh), hp_lr4(xoHigh));

wetStage1(x) = x : band3split
    : (ottBand(lowThreshAboveDb,  lowThreshBelowDb,  lowThreshExpandDb,
               lowRatioAbove,  lowRatioBelow,  lowExpandRatio,  depth, time),
       ottBand(midThreshAboveDb,  midThreshBelowDb,  midThreshExpandDb,
               midRatioAbove,  midRatioBelow,  midExpandRatio,  depth, time),
       ottBand(highThreshAboveDb, highThreshBelowDb, highThreshExpandDb,
               highRatioAbove, highRatioBelow, highExpandRatio, depth, time))
    : *(db2lin(lowGainDb)), *(db2lin(midGainDb)), *(db2lin(highGainDb))
    :> _;

wetStage2(x) = x : band3split
    : (ottBand(lowThreshAboveDb,  lowThreshBelowDb,  lowThreshExpandDb,
               lowRatioAbove,  lowRatioBelow,  lowExpandRatio,  depth2, time2),
       ottBand(midThreshAboveDb,  midThreshBelowDb,  midThreshExpandDb,
               midRatioAbove,  midRatioBelow,  midExpandRatio,  depth2, time2),
       ottBand(highThreshAboveDb, highThreshBelowDb, highThreshExpandDb,
               highRatioAbove, highRatioBelow, highExpandRatio, depth2, time2))
    :> _;

drySplit(x) = x : band3split :> _;

channel(x) = ((1.0 - mix) * drySplit(x) + mix * (wetStage1(x) : wetStage2)) * db2lin(outGainDb);

process = par(i, 2, channel);
