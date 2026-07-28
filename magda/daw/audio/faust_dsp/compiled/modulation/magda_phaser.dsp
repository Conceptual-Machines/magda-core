declare name "MagdaPhaser";
declare description "Stereo phaser — sweeping notch comb with switchable stage count and feedback.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

rate     = hslider("Rate [unit:Hz] [scale:log] [scaleAnchor:1] [idx:0]",
                   0.5, 0.05, 10.0, 0.01)
         : si.smooth(ba.tau2pole(0.05));

// Sweep depth. 0..1 is conventional phasing; 2 turns it into pure-allpass
// vibrato (per Faust's phaser2 docs).
depth    = hslider("Depth [idx:1]", 1.0, 0.0, 2.0, 0.01)
         : si.smooth(ba.tau2pole(0.02));

feedback = hslider("Feedback [idx:2]", 0.3, -0.95, 0.95, 0.01)
         : si.smooth(ba.tau2pole(0.02));

// Stage count drives notch density. Faust's phaser2 takes Notches as a
// macro arg, so we run all four stage counts in parallel and pick one.
stages   = nentry("Stages [idx:3] [style:menu{'2':0;'4':1;'6':2;'8':3}]",
                  1, 0, 3, 1);

frqMin   = hslider("Min Hz [unit:Hz] [scale:log] [scaleAnchor:200] [idx:4]",
                   100, 30, 1000, 1)
         : si.smooth(ba.tau2pole(0.05));

frqMax   = hslider("Max Hz [unit:Hz] [scale:log] [scaleAnchor:2000] [idx:5]",
                   2000, 500, 8000, 1)
         : si.smooth(ba.tau2pole(0.05));

mix      = hslider("Mix [idx:6]", 0.6, 0.0, 1.0, 0.001)
         : si.smooth(ba.tau2pole(0.02));

// ============================================================================
// DSP
// ============================================================================

// Notch width is mostly cosmetic at modest feedback; pick a tasteful value.
notchWidth = 1000;
fratio     = 1.5;

phaserN(N) = pf.phaser2_stereo(N, notchWidth, frqMin, fratio, frqMax,
                                rate, depth, feedback, 0);

// Stereo wet path — selectn over the four stage counts. CPU is small;
// only one branch is heard.
wet = _,_ <: (phaserN(2), phaserN(4), phaserN(6), phaserN(8))
           : ro.interleave(2, 4)
           : ba.selectn(4, int(stages)), ba.selectn(4, int(stages));

// Mix dry/wet per channel.
mixCh(d, w) = d * (1.0 - mix) + w * mix;

process = _,_ <: (_,_), wet : ro.interleave(2, 2) : mixCh, mixCh;
