declare name "MagdaGate";
declare description "Stereo gate/downward expander with linked detector, range, parallel mix, and output trim.";

import("stdfaust.lib");

// Knob-only controls (slots 0-3) — shown in param grid.
attackMs  = hslider("Attack [unit:ms] [scale:log] [scaleAnchor:1] [idx:0]",  1.0,   0.1,  100.0, 0.1) : si.smooth(ba.tau2pole(0.02));
releaseMs = hslider("Release [unit:ms] [scale:log] [scaleAnchor:100] [idx:1]", 120.0, 5.0, 1000.0, 1.0) : si.smooth(ba.tau2pole(0.02));
mix       = hslider("Mix [idx:2]",                                              1.0, 0.0,    1.0, 0.001) : si.smooth(ba.tau2pole(0.02));
outputDb  = hslider("Output [unit:dB] [idx:3]",                                 0.0, -24.0, 24.0, 0.1)  : si.smooth(ba.tau2pole(0.02));

// Curve-editor controls (slots 4-6) — hidden from knob grid.
thresholdDb = hslider("Threshold [unit:dB] [idx:4]", -40.0, -80.0,  0.0, 0.1) : si.smooth(ba.tau2pole(0.02));
ratio       = hslider("Ratio [scale:log] [scaleAnchor:4] [idx:5]", 4.0, 1.0, 50.0, 0.01) : si.smooth(ba.tau2pole(0.02));
rangeDb     = hslider("Range [unit:dB] [idx:6]",        60.0,  0.0, 80.0, 0.1)  : si.smooth(ba.tau2pole(0.02));

db2lin(db) = pow(10.0, db / 20.0);
attackS = max(0.0001, attackMs * 0.001);
releaseS = max(0.001, releaseMs * 0.001);

detector(l, r) = max(abs(l), abs(r)) : si.lag_ud(attackS, releaseS) : max(ma.EPSILON) : ba.linear2db;

// Below threshold, ratio controls how aggressively the signal closes. Range
// caps attenuation so low ratios can be used as an expander and high ratios
// become a practical gate.
gainDb(levelDb) = 0.0 - min(rangeDb, max(0.0, thresholdDb - levelDb) * (max(1.0, ratio) - 1.0));

process(l, r) = outL, outR
with {
    g = detector(l, r) : gainDb : db2lin;
    wetL = l * g;
    wetR = r * g;
    outL = ((1.0 - mix) * l + mix * wetL) * db2lin(outputDb);
    outR = ((1.0 - mix) * r + mix * wetR) * db2lin(outputDb);
};
