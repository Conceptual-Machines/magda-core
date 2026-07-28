declare name "MagdaMultiband";
declare description "OTT-style 3-band compressor: LR4 split, two OTT stages in series per band, symmetric expander, per-band brickwall limiter.";

import("stdfaust.lib");

// ============================================================================
// User controls
// Slots 0-8:  knob-only (not editable in the curve editor).
// Slots 9-35: per-band controls edited directly on the curve view.
// Slots 36-37: crossover frequencies, editor-only (hidden from knob grid).
// ============================================================================

// Master controls.
depth  = hslider("Depth [idx:0]",   1.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
time   = hslider("Time [idx:1]",    0.4, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
attack = hslider("Attack [idx:2]",  0.0, 0.0, 1.0, 0.001) : si.smooth(ba.tau2pole(0.05));
inputGainDb = hslider("Input [unit:dB] [idx:3]", 0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.02));

// Per-band post-compression makeup gain.
lowGainDb  = hslider("Low Gain [unit:dB] [idx:4]",  0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));
midGainDb  = hslider("Mid Gain [unit:dB] [idx:5]",  0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));
highGainDb = hslider("High Gain [unit:dB] [idx:6]", 0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));

mix       = hslider("Mix [idx:7]",              1.0, 0.0, 1.0,   0.001) : si.smooth(ba.tau2pole(0.05));
outGainDb = hslider("Output [unit:dB] [idx:8]", 0.0, -24.0, 24.0, 0.1) : si.smooth(ba.tau2pole(0.05));

// Per-band editor controls — low band (slots 9-17).
lowThreshAboveDb       = hslider("Low Thresh Above [unit:dB] [idx:9]",         -24.0, -60.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
lowThreshBelowDb       = hslider("Low Thresh Below [unit:dB] [idx:10]",        -48.0, -80.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
lowRatioAbove          = hslider("Low Ratio Above [idx:11]",                    8.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
lowRatioBelow          = hslider("Low Ratio Below [idx:12]",                    8.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
lowThreshExpandBelowDb = hslider("Low Thresh Expand Below [unit:dB] [idx:13]", -72.0, -80.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
lowExpandRatioBelow    = hslider("Low Expand Ratio Below [idx:14]",              1.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
lowThreshExpandAboveDb = hslider("Low Thresh Expand Above [unit:dB] [idx:15]",  0.0, -60.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
lowExpandRatioAbove    = hslider("Low Expand Ratio Above [idx:16]",              1.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
lowLimitDb             = hslider("Low Limit [unit:dB] [idx:17]",                0.0, -24.0, 12.0,  0.1) : si.smooth(ba.tau2pole(0.005));

// Per-band editor controls — mid band (slots 18-26).
midThreshAboveDb       = hslider("Mid Thresh Above [unit:dB] [idx:18]",        -24.0, -60.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
midThreshBelowDb       = hslider("Mid Thresh Below [unit:dB] [idx:19]",        -48.0, -80.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
midRatioAbove          = hslider("Mid Ratio Above [idx:20]",                    8.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
midRatioBelow          = hslider("Mid Ratio Below [idx:21]",                    8.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
midThreshExpandBelowDb = hslider("Mid Thresh Expand Below [unit:dB] [idx:22]", -72.0, -80.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
midExpandRatioBelow    = hslider("Mid Expand Ratio Below [idx:23]",              1.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
midThreshExpandAboveDb = hslider("Mid Thresh Expand Above [unit:dB] [idx:24]",  0.0, -60.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
midExpandRatioAbove    = hslider("Mid Expand Ratio Above [idx:25]",              1.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
midLimitDb             = hslider("Mid Limit [unit:dB] [idx:26]",                0.0, -24.0, 12.0,  0.1) : si.smooth(ba.tau2pole(0.005));

// Per-band editor controls — high band (slots 27-35).
highThreshAboveDb       = hslider("High Thresh Above [unit:dB] [idx:27]",        -24.0, -60.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
highThreshBelowDb       = hslider("High Thresh Below [unit:dB] [idx:28]",        -48.0, -80.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
highRatioAbove          = hslider("High Ratio Above [idx:29]",                    8.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
highRatioBelow          = hslider("High Ratio Below [idx:30]",                    8.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
highThreshExpandBelowDb = hslider("High Thresh Expand Below [unit:dB] [idx:31]", -72.0, -80.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
highExpandRatioBelow    = hslider("High Expand Ratio Below [idx:32]",              1.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
highThreshExpandAboveDb = hslider("High Thresh Expand Above [unit:dB] [idx:33]",  0.0, -60.0,  0.0,  0.1) : si.smooth(ba.tau2pole(0.05));
highExpandRatioAbove    = hslider("High Expand Ratio Above [idx:34]",              1.0,   1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.05));
highLimitDb             = hslider("High Limit [unit:dB] [idx:35]",                0.0, -24.0, 12.0,  0.1) : si.smooth(ba.tau2pole(0.005));

// Crossover frequencies — editor-only, hidden from knob grid.
xoLow  = hslider("Low XO [unit:Hz] [scale:log] [scaleAnchor:200] [idx:36]",   120, 40,  500, 1) : si.smooth(ba.tau2pole(0.05));
xoHigh = hslider("High XO [unit:Hz] [scale:log] [scaleAnchor:2000] [idx:37]", 2500, 500, 8000, 1) : si.smooth(ba.tau2pole(0.05));

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

// Attack knob: 0.1ms (min) to 50ms (max). Time knob: release 5ms-250ms.
attS(a) = 0.0001 + 0.0499 * a;
relS(t) = 0.005  + 0.245  * t;

envFollow(x) = abs(x) : si.lag_ud(attS(attack), relS(time));

// Four-zone gain computer (all terms additive, depth-scaled).
downGainDb(thr, r, lvl)       = max(0.0, lvl - thr) * (1.0 / max(1.0, r) - 1.0);
upGainDb(thr, r, lvl)         = max(0.0, thr - lvl) * (1.0 - 1.0 / max(1.0, r));
expandBelowGainDb(thr, r, lvl) = max(0.0, thr - lvl) * (1.0 / max(1.0, r) - 1.0);
expandAboveGainDb(thr, r, lvl) = max(0.0, lvl - thr) * (1.0 - 1.0 / max(1.0, r));

combinedGainDb(thrAbove, thrBelow, thrExpBelow, thrExpAbove,
               rAbove, rBelow, rExpBelow, rExpAbove, d, lvl) =
    (downGainDb(thrAbove, rAbove, lvl)
     + upGainDb(thrBelow, rBelow, lvl)
     + expandBelowGainDb(thrExpBelow, rExpBelow, lvl)
     + expandAboveGainDb(thrExpAbove, rExpAbove, lvl)) * d;

ottBand(thrAbove, thrBelow, thrExpBelow, thrExpAbove,
        rAbove, rBelow, rExpBelow, rExpAbove, d, x) =
    x * (envFollow(x)
         : ba.linear2db
         : combinedGainDb(thrAbove, thrBelow, thrExpBelow, thrExpAbove,
                          rAbove, rBelow, rExpBelow, rExpAbove, d)
         : db2lin);

// Instantaneous brickwall limiter.
hardLimit(limDb, x) = x * min(1.0, db2lin(limDb) / max(1e-6, abs(x)));

lp_lr4(fc) = fi.lowpass(2, fc) : fi.lowpass(2, fc);
hp_lr4(fc) = fi.highpass(2, fc) : fi.highpass(2, fc);
band3split  = _ <: lp_lr4(xoLow), (hp_lr4(xoLow) <: lp_lr4(xoHigh), hp_lr4(xoHigh));

// Second stage is internal and tracks Depth. This keeps one main intensity
// control while making new instances hit closer to OTT-style multiband action.
stage2Depth = min(1.0, depth * 0.75);

// Two OTT stages in series within the same band, then limit, then makeup.
bandProcess(thrAbove, thrBelow, thrExpBelow, thrExpAbove,
            rAbove, rBelow, rExpBelow, rExpAbove, gainDb, limDb, x) =
    x : ottBand(thrAbove, thrBelow, thrExpBelow, thrExpAbove,
                rAbove, rBelow, rExpBelow, rExpAbove, depth)
      : ottBand(thrAbove, thrBelow, thrExpBelow, thrExpAbove,
                rAbove, rBelow, rExpBelow, rExpAbove, stage2Depth)
      : hardLimit(limDb)
      : *(db2lin(gainDb));

wet(x) = x : band3split :
    (bandProcess(lowThreshAboveDb,  lowThreshBelowDb,  lowThreshExpandBelowDb,  lowThreshExpandAboveDb,
                 lowRatioAbove,  lowRatioBelow,  lowExpandRatioBelow,  lowExpandRatioAbove,  lowGainDb,  lowLimitDb),
     bandProcess(midThreshAboveDb,  midThreshBelowDb,  midThreshExpandBelowDb,  midThreshExpandAboveDb,
                 midRatioAbove,  midRatioBelow,  midExpandRatioBelow,  midExpandRatioAbove,  midGainDb,  midLimitDb),
     bandProcess(highThreshAboveDb, highThreshBelowDb, highThreshExpandBelowDb, highThreshExpandAboveDb,
                 highRatioAbove, highRatioBelow, highExpandRatioBelow, highExpandRatioAbove, highGainDb, highLimitDb))
    :> _;

driveInput(x) = x * db2lin(inputGainDb);
splitDry(x) = x : band3split :> _;
channel(x) = ((1.0 - mix) * splitDry(driveInput(x)) + mix * wet(driveInput(x))) *
             db2lin(outGainDb);

process = par(i, 2, channel);
