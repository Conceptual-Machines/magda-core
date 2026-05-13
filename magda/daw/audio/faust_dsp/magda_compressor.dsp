declare name "MagdaCompressor";
declare description "Clean stereo feed-forward compressor with peak/RMS detection, soft knee, stereo link, sidechain high-pass, parallel mix, and output safety limiting.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

thresholdDb = hslider("Threshold [unit:dB] [idx:0]", -18.0, -60.0, 0.0, 0.1)
              : si.smooth(ba.tau2pole(0.02));
ratio = hslider("Ratio [idx:1]", 4.0, 1.0, 20.0, 0.01)
        : si.smooth(ba.tau2pole(0.02));
attackMs = hslider("Attack [unit:ms] [scale:log] [scaleAnchor:10] [idx:2]",
                   10.0, 0.1, 200.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));
releaseMs = hslider("Release [unit:ms] [scale:log] [scaleAnchor:100] [idx:3]",
                    120.0, 5.0, 1000.0, 1.0)
            : si.smooth(ba.tau2pole(0.02));
kneeDb = hslider("Knee [unit:dB] [idx:4]", 6.0, 0.0, 24.0, 0.1)
         : si.smooth(ba.tau2pole(0.02));
makeupDb = hslider("Makeup [unit:dB] [idx:5]", 0.0, 0.0, 24.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));
mix = hslider("Mix [idx:6]", 1.0, 0.0, 1.0, 0.001)
      : si.smooth(ba.tau2pole(0.02));
outputDb = hslider("Output [unit:dB] [idx:7]", 0.0, -24.0, 12.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));
detector = nentry("Detector [idx:8] [style:menu{'Peak':0;'RMS':1}]",
                  0, 0, 1, 1);
link = hslider("Link [idx:9]", 1.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));
sidechainHpfHz = hslider("SC HPF [unit:Hz] [scale:log] [scaleAnchor:120] [idx:10]",
                         20.0, 20.0, 500.0, 1.0)
                 : si.smooth(ba.tau2pole(0.02));
useSidechain = nentry("Use Sidechain [hidden:1] [idx:63]", 0, 0, 1, 1);

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

attackS = max(0.0001, attackMs * 0.001);
releaseS = max(0.001, releaseMs * 0.001);
strength = 1.0 - (1.0 / max(1.0, ratio));

// The detector path is filtered, but the audio path remains full-band.
detectorPre(x) = x : fi.highpass(1, sidechainHpfHz);

softLimit(x) = ma.tanh(x * 0.75) / ma.tanh(0.75);

square(x) = x * x;

gainDbFromLevel(levelDb) =
    select3((levelDb > (thresholdDb - kneeDb * 0.5))
            + (levelDb > (thresholdDb + kneeDb * 0.5)),
            0.0,
            -strength * square(levelDb - thresholdDb + kneeDb * 0.5)
                / (2.0 * max(ma.EPSILON, kneeDb)),
            -strength * (levelDb - thresholdDb));

peakGainDb(x) = x
    : detectorPre
    : abs
    : si.lag_ud(attackS, releaseS)
    : max(ma.EPSILON)
    : ba.linear2db
    : gainDbFromLevel;

rmsGainDb(x) = x
    : detectorPre
    : square
    : si.lag_ud(attackS, releaseS)
    : max(ma.EPSILON)
    : pow(0.5)
    : ba.linear2db
    : gainDbFromLevel;

detectorGainDb(x) = peakGainDb(x) * (1.0 - detector) + rmsGainDb(x) * detector;
linkedGainDbL(l, r) = detectorGainDb(l) * (1.0 - link)
                      + min(detectorGainDb(l), detectorGainDb(r)) * link;
linkedGainDbR(l, r) = detectorGainDb(r) * (1.0 - link)
                      + min(detectorGainDb(l), detectorGainDb(r)) * link;

internalWetL(l, r) = l * db2lin(linkedGainDbL(l, r));
internalWetR(l, r) = r * db2lin(linkedGainDbR(l, r));
externalWetL(l, sc) = l * db2lin(detectorGainDb(sc));
externalWetR(r, sc) = r * db2lin(detectorGainDb(sc));

compress(l, r, sc) = internalWetL(l, r) * (1.0 - useSidechain)
                     + externalWetL(l, sc) * useSidechain,
                     internalWetR(l, r) * (1.0 - useSidechain)
                     + externalWetR(r, sc) * useSidechain;

channelBlend(dry, wet) = (dry * (1.0 - mix) + wet * mix) * db2lin(makeupDb + outputDb)
                         : softLimit;

wetL(l, r, sc) = compress(l, r, sc) : _, !;
wetR(l, r, sc) = compress(l, r, sc) : !, _;

process(l, r, sc) = channelBlend(l, wetL(l, r, sc)), channelBlend(r, wetR(l, r, sc));
