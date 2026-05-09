declare name "MagdaDualFilter";
declare description "Dual filter — two SVF/Ladder/VA blocks, switchable serial or parallel routing.";

import("stdfaust.lib");

// ============================================================================
// Layout
// ============================================================================
// Faust devices route through FaustDeviceLayout which honours [idx:N]
// directly — pool slot N lands at grid cell N. Filter A occupies row 0
// columns 0-3, Filter B occupies row 1 columns 0-3, globals trail on
// row 0 columns 4-6 with empty cells on the right.
//
//   row 0: [A Cut]  [A Res]  [A Eng]  [A Mode]  [Routing] [Balance] [Drive]  [   ]
//   row 1: [B Cut]  [B Res]  [B Eng]  [B Mode]  [       ] [       ] [     ]  [   ]

// ============================================================================
// User controls — Filter A (row 0)
// ============================================================================

cutoffA = hslider("A Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                  1000, 20, 20000, 1)
        : si.smooth(ba.tau2pole(0.02));

resA    = hslider("A Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
        : si.smooth(ba.tau2pole(0.02));

engineA = nentry("A Engine [idx:2] [style:menu{'SVF':0;'Ladder':1;'Korg35 LP':2;'Korg35 HP':3;'Diode':4;'Oberheim LP':5;'Oberheim BP':6;'Oberheim HP':7;'SK LP':8;'SK BP':9}]",
                 0, 0, 9, 1);

// SVF mode for A. Greyed when engineA != SVF (gate:!2).
modeA   = nentry("A Mode [idx:3] [gate:!2] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                 0, 0, 3, 1);

// ============================================================================
// User controls — globals (row 0, right of A)
// ============================================================================

// Routing: 0 = Serial (A → B), 1 = Parallel (A and B summed via Balance).
routing = nentry("Routing [idx:4] [style:menu{'Serial':0;'Parallel':1}]",
                 0, 0, 1, 1);

// Balance A↔B for parallel mode. Greyed in serial mode (active when
// Routing != 0, i.e. gate:4 is on).
balance = hslider("Balance [idx:5] [gate:4]", 0.5, 0.0, 1.0, 0.001)
        : si.smooth(ba.tau2pole(0.02));

// Pre-filter saturation amount (shared). 0 = clean bypass; 1 = full tanh.
drive   = hslider("Drive [idx:6]", 0.0, 0.0, 1.0, 0.001)
        : si.smooth(ba.tau2pole(0.02));

// idx 7 deliberately left empty — visual breathing room before row 1.

// ============================================================================
// User controls — Filter B (row 1)
// ============================================================================

cutoffB = hslider("B Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:8]",
                  1000, 20, 20000, 1)
        : si.smooth(ba.tau2pole(0.02));

resB    = hslider("B Resonance [idx:9]", 0.0, 0.0, 1.0, 0.001)
        : si.smooth(ba.tau2pole(0.02));

engineB = nentry("B Engine [idx:10] [style:menu{'SVF':0;'Ladder':1;'Korg35 LP':2;'Korg35 HP':3;'Diode':4;'Oberheim LP':5;'Oberheim BP':6;'Oberheim HP':7;'SK LP':8;'SK BP':9}]",
                 0, 0, 9, 1);

modeB   = nentry("B Mode [idx:11] [gate:!10] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                 0, 0, 3, 1);

// ============================================================================
// DSP
// ============================================================================

// Resonance maps — same per-engine ranges as the single-filter device so
// character matches between the two devices.
qSVF(r)    = 0.5 + r * 19.5;
qLadder(r) = r * 0.99;
qVA(r)     = 0.7 + r * 9.3;
qDiode(r)  = 0.7 + r * 19.3;
qOb(r)     = 0.5 + r * 9.5;

// Drive: dry/saturated lerp; 0 = bypass, 1 = full tanh, normalised by tanh(4)
// so unity-amplitude input stays unity at full drive.
drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// One filter block. Every engine path is evaluated in parallel and selectn
// picks the heard one. Each path is small (2-pole SVF / biquad VA), so the
// total cost per block stays modest even with 10 engines.
filterBlock(cutoff, res, engine, mode, x) =
    (svfPath, ladderPath,
     korg35LPPath, korg35HPPath,
     diodePath,
     obLPPath, obBPPath, obHPPath,
     skLPPath, skBPPath) : ba.selectn(10, int(engine))
with {
    // `ve.*` VA filters take a 0..1 log-mapped control — library remaps it
    // internally as `freq = 2 * 10^(3*normFreq + 1)`. Invert so the per-block
    // Cutoff knob drives them in Hz. SVF and Moog ladder accept Hz directly.
    nf         = log(cutoff / 20.0) / log(1000.0);
    svfPath    = ((x : fi.svf.lp(cutoff, qSVF(res))),
                  (x : fi.svf.bp(cutoff, qSVF(res))),
                  (x : fi.svf.hp(cutoff, qSVF(res))),
                  (x : fi.svf.notch(cutoff, qSVF(res)))) : ba.selectn(4, int(mode));
    ladderPath   = x : ve.moog_vcf_2bn(qLadder(res), cutoff);
    korg35LPPath = x : ve.korg35LPF(nf, qVA(res));
    korg35HPPath = x : ve.korg35HPF(nf, qVA(res));
    diodePath    = x : ve.diodeLadder(nf, qDiode(res));
    obLPPath     = x : ve.oberheimLPF(nf, qOb(res));
    obBPPath     = x : ve.oberheimBPF(nf, qOb(res));
    obHPPath     = x : ve.oberheimHPF(nf, qOb(res));
    skLPPath     = x : ve.sallenKey2ndOrderLPF(nf, qVA(res));
    skBPPath     = x : ve.sallenKey2ndOrderBPF(nf, qVA(res));
};

filterA(x) = filterBlock(cutoffA, resA, engineA, modeA, x);
filterB(x) = filterBlock(cutoffB, resB, engineB, modeB, x);

// Routing:
//   serial   = drive → A → B
//   parallel = drive → A and drive → B, mixed linearly via Balance.
serialChain(x)   = drivenIn(x) : filterA : filterB;
parallelChain(x) = filterA(d) * (1.0 - balance) + filterB(d) * balance
with {
    d = drivenIn(x);
};

routedChain(x) = (serialChain(x), parallelChain(x)) : ba.selectn(2, int(routing));

// Stereo: independent filter state per channel.
process = par(i, 2, routedChain);
