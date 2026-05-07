declare name "MagdaFilter";
declare description "Stereo filter — switchable SVF (LP/BP/HP/Notch) and Moog ladder engines, with pre-filter drive saturation.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Cutoff frequency, log scale 20 Hz – 20 kHz.
// Cutoff. `[scale:log]` selects log projection in both the slider's
// drag math and ParameterUtils' automation interpolation;
// `[scaleAnchor:1000]` then skews the curve so 1 kHz lands at the
// slider's pixel midpoint.
cutoff = hslider("Cutoff [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
                 1000, 20, 20000, 1)
       : si.smooth(ba.tau2pole(0.02));

// Normalised resonance 0..1 — re-scaled per engine below so the same knob
// position feels comparable across SVF and ladder modes.
res    = hslider("Resonance [idx:1]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// Pre-filter saturation amount. 0 = clean bypass; 1 = full tanh shaping.
drive  = hslider("Drive [idx:2]", 0.0, 0.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// Filter topology. 0 = SVF (clean, full mode set). 1 = Moog ladder
// (LP-only, characterful — pushes Mode into a greyed-out state via
// the gate annotation below).
engine = nentry("Engine [idx:3] [style:menu{'SVF':0;'Ladder':1}]",
                 0, 0, 1, 1);

// SVF mode select. Greyed out when Engine is set to Ladder, since the
// ladder engine ignores Mode (it's structurally LP-only).
mode   = nentry("Mode [idx:4] [gate:!3] [style:menu{'LP':0;'BP':1;'HP':2;'Notch':3}]",
                 0, 0, 3, 1);

// ============================================================================
// DSP
// ============================================================================

// SVF Q maps 0.5 (gentle) .. 20 (screaming, near self-osc).
qSVF    = 0.5 + res * 19.5;

// Ladder resonance 0..0.99 — stay short of 1.0 to avoid the
// self-oscillating runaway state at extremes.
qLadder = res * 0.99;

// Drive: dry/saturated lerp so drive=0 is a true bypass. The tanh
// branch is normalised by tanh(4) so unity-amplitude signals stay
// at unity at full drive (no hidden makeup-gain bump).
drivenIn(x) = (1.0 - drive) * x
            + drive * (ma.tanh(4.0 * x) / ma.tanh(4.0));

// SVF bank: run all four modes in parallel and pick one with selectn.
// Yes, this evaluates all four — Faust does no lazy mode-switching —
// but each is a 2-pole filter, total CPU is negligible.
svfOut(x) = ((x : fi.svf.lp(cutoff, qSVF)),
             (x : fi.svf.bp(cutoff, qSVF)),
             (x : fi.svf.hp(cutoff, qSVF)),
             (x : fi.svf.notch(cutoff, qSVF))) : ba.selectn(4, int(mode));

// Moog ladder — LP only.
ladderOut(x) = x : ve.moog_vcf(qLadder, cutoff);

// Engine select. Same selectn-evaluates-all caveat; both engines run,
// only one is heard.
engineOut(x) = (svfOut(driven), ladderOut(driven)) : ba.selectn(2, int(engine))
with {
    driven = drivenIn(x);
};

// Stereo: independent filter state per channel.
process = par(i, 2, engineOut);
