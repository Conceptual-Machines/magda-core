declare name "MagdaEq";
declare description "8-band parametric equaliser with per-band filter type (HP / LowShelf / Bell / HighShelf / LP / Notch).";

import("stdfaust.lib");
wa = library("webaudio.lib");

// ============================================================================
// Per-band controls — 8 bands × {Type, Freq, Gain, Q}.
// Slot indices follow [idx:N] where N = 4*band + role, with
//   role: 0=Type 1=Freq 2=Gain 3=Q.
// Type values: 0=HP 1=LowShelf 2=Bell 3=HighShelf 4=LP 5=Notch.
// LS/HS use cookbook biquads that ignore Q; HP/LP/Notch ignore Gain.
// Bands are declared explicitly so each band can carry its own defaults.
// ============================================================================

b1Type = nentry("Band1 Type [idx:0] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 0, 0, 5, 1);
b1Freq = hslider("Band1 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:1]", 30, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b1Gain = hslider("Band1 Gain [unit:dB] [idx:2]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b1Q    = hslider("Band1 Q [idx:3]", 0.707, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b2Type = nentry("Band2 Type [idx:4] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 1, 0, 5, 1);
b2Freq = hslider("Band2 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:5]", 100, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b2Gain = hslider("Band2 Gain [unit:dB] [idx:6]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b2Q    = hslider("Band2 Q [idx:7]", 0.707, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b3Type = nentry("Band3 Type [idx:8] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 2, 0, 5, 1);
b3Freq = hslider("Band3 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:9]", 250, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b3Gain = hslider("Band3 Gain [unit:dB] [idx:10]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b3Q    = hslider("Band3 Q [idx:11]", 1.0, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b4Type = nentry("Band4 Type [idx:12] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 2, 0, 5, 1);
b4Freq = hslider("Band4 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:13]", 800, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b4Gain = hslider("Band4 Gain [unit:dB] [idx:14]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b4Q    = hslider("Band4 Q [idx:15]", 1.0, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b5Type = nentry("Band5 Type [idx:16] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 2, 0, 5, 1);
b5Freq = hslider("Band5 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:17]", 2000, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b5Gain = hslider("Band5 Gain [unit:dB] [idx:18]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b5Q    = hslider("Band5 Q [idx:19]", 1.0, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b6Type = nentry("Band6 Type [idx:20] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 2, 0, 5, 1);
b6Freq = hslider("Band6 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:21]", 5000, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b6Gain = hslider("Band6 Gain [unit:dB] [idx:22]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b6Q    = hslider("Band6 Q [idx:23]", 1.0, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b7Type = nentry("Band7 Type [idx:24] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 3, 0, 5, 1);
b7Freq = hslider("Band7 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:25]", 10000, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b7Gain = hslider("Band7 Gain [unit:dB] [idx:26]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b7Q    = hslider("Band7 Q [idx:27]", 0.707, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

b8Type = nentry("Band8 Type [idx:28] [style:menu{'HP':0;'LowShelf':1;'Bell':2;'HighShelf':3;'LP':4;'Notch':5}]", 4, 0, 5, 1);
b8Freq = hslider("Band8 Freq [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:29]", 18000, 20, 20000, 0.1) : si.smooth(ba.tau2pole(0.03));
b8Gain = hslider("Band8 Gain [unit:dB] [idx:30]", 0, -24, 24, 0.1) : si.smooth(ba.tau2pole(0.03));
b8Q    = hslider("Band8 Q [idx:31]", 0.707, 0.1, 10.0, 0.001) : si.smooth(ba.tau2pole(0.03));

outputDb = hslider("Output [unit:dB] [idx:32]", 0, -24, 12, 0.1) : si.smooth(ba.tau2pole(0.02));

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

// Clamp the working frequency under Nyquist — webaudio biquads can become
// ill-conditioned when fc approaches SR/2 (Q knob amplifies the issue).
safeFreq(f) = min(f, ma.SR * 0.45);

// Per-band stage: every filter type is instantiated in parallel and the
// Type nentry picks one with ba.selectn. ~6× the biquad cost per band, but
// each biquad is trivial and the parallel layout makes Type a zero-glitch
// switch (the inactive filters run on dummy input but aren't summed).
bandFilter(t, fc, gDb, q, x) = (
    (x : wa.highpass2(safeFreq(fc), q, 0)),
    (x : wa.lowshelf2(safeFreq(fc), gDb, 0)),
    (x : wa.peaking2(safeFreq(fc), gDb, q, 0)),
    (x : wa.highshelf2(safeFreq(fc), gDb, 0)),
    (x : wa.lowpass2(safeFreq(fc), q, 0)),
    (x : wa.notch2(safeFreq(fc), q, 0))
) : ba.selectn(6, int(t));

// Bands run serially on each channel. Channels are independent (no
// stereo-linked detector).
eqMono = bandFilter(b1Type, b1Freq, b1Gain, b1Q)
       : bandFilter(b2Type, b2Freq, b2Gain, b2Q)
       : bandFilter(b3Type, b3Freq, b3Gain, b3Q)
       : bandFilter(b4Type, b4Freq, b4Gain, b4Q)
       : bandFilter(b5Type, b5Freq, b5Gain, b5Q)
       : bandFilter(b6Type, b6Freq, b6Gain, b6Q)
       : bandFilter(b7Type, b7Freq, b7Gain, b7Q)
       : bandFilter(b8Type, b8Freq, b8Gain, b8Q);

process = par(c, 2, eqMono) : par(c, 2, *(db2lin(outputDb)));
