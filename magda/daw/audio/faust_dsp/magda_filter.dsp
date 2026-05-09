declare name "MagdaFilter";
declare description "Stereo filter — switchable SVF, Moog ladder, Korg35, Diode, Oberheim, Sallen-Key engines with pre-filter drive saturation.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Cutoff. `[scale:log]` selects log projection in both the slider's
// drag math and ParameterUtils' automation interpolation;
// `[scaleAnchor:1000]` then skews the curve so 1 kHz lands at the
// slider's pixel midpoint.
cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 20, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

// Normalised resonance 0..1 — re-scaled per engine below so the same knob
// position feels comparable across all topologies.
res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// Pre-filter saturation amount. 0 = clean bypass; 1 = full tanh shaping.
drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// Filter topology. SVF is the clean multi-mode option; everything else is a
// virtual-analog model with its own LP/BP/HP variant baked in. Mode is only
// meaningful when SVF is selected (gated below).
engine = nentry("Engine [idx:3] [style:menu{'SVF':0;'Ladder':1;'Korg35 LP':2;'Korg35 HP':3;'Diode':4;'Oberheim LP':5;'Oberheim BP':6;'Oberheim HP':7;'SK LP':8;'SK BP':9}]",
                 0, 0, 9, 1);

// SVF mode select. Greyed out for any engine other than SVF (gate:!3 is
// active only when engine == 0).
mode   = nentry("Mode [idx:4] [gate:!3] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                 0, 0, 3, 1);

// ============================================================================
// DSP
// ============================================================================

// `ve.*` virtual-analog filters (Korg35 / Diode / Oberheim / Sallen-Key)
// take a 0..1 control where the library internally remaps to Hz via
// `freq = 2 * 10^(3*normFreq + 1)` — so 0..1 spans 20 Hz..20 kHz on a
// log curve. Invert that here so the Cutoff knob's Hz value drives the
// filters as expected. SVF and the Moog ladder accept Hz directly and
// stay on `cutoff`.
nf       = log(cutoff / 20.0) / log(1000.0);

// Per-engine resonance ranges, hand-mapped so a `res` of ~0.7 sits at the
// "starting to ring" point in each topology and ~0.95+ approaches self-osc.
qSVF     = 0.5 + res * 19.5;       // SVF: 0.5..20
qLadder  = res * 0.99;             // Moog ladder normalised, capped under 1
qVA      = 0.7 + res * 9.3;        // Korg35 / Sallen-Key: 0.7..10
qDiode   = 0.7 + res * 19.3;       // Diode: 0.7..20 (built-in saturation)
qOb      = 0.5 + res * 9.5;        // Oberheim SEM: 0.5..10

// Drive: dry/saturated lerp so drive=0 is a true bypass. The tanh
// branch is normalised by tanh(4) so unity-amplitude signals stay
// at unity at full drive (no hidden makeup-gain bump).
drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// SVF bank: run all four modes in parallel and pick one with selectn.
// Each is a 2-pole filter — total CPU is negligible.
svfOut(x)      = ((x : fi.svf.lp(cutoff, qSVF)),
                  (x : fi.svf.bp(cutoff, qSVF)),
                  (x : fi.svf.hp(cutoff, qSVF)),
                  (x : fi.svf.notch(cutoff, qSVF))) : ba.selectn(4, int(mode));

// Moog ladder — LP only. The protected normalized-ladder biquad is stable
// across the full audible range (the simpler `moog_vcf` blows up past SR/2π).
ladderOut(x)   = x : ve.moog_vcf_2bn(qLadder, cutoff);

// Virtual-analog topologies. Each is mono-in / mono-out and takes
// `(normFreq, Q)`.
korg35LPOut(x) = x : ve.korg35LPF(nf, qVA);
korg35HPOut(x) = x : ve.korg35HPF(nf, qVA);
diodeOut(x)    = x : ve.diodeLadder(nf, qDiode);
obLPOut(x)     = x : ve.oberheimLPF(nf, qOb);
obBPOut(x)     = x : ve.oberheimBPF(nf, qOb);
obHPOut(x)     = x : ve.oberheimHPF(nf, qOb);
skLPOut(x)     = x : ve.sallenKey2ndOrderLPF(nf, qVA);
skBPOut(x)     = x : ve.sallenKey2ndOrderBPF(nf, qVA);

// Engine select. `selectn` evaluates every branch — but each filter is small,
// total cost is well under 1% of a CPU core at 48 kHz.
engineOut(x) = (svfOut(driven), ladderOut(driven),
                korg35LPOut(driven), korg35HPOut(driven),
                diodeOut(driven),
                obLPOut(driven), obBPOut(driven), obHPOut(driven),
                skLPOut(driven), skBPOut(driven)) : ba.selectn(10, int(engine))
with {
    driven = drivenIn(x);
};

// Stereo: independent filter state per channel.
process = par(i, 2, engineOut);
