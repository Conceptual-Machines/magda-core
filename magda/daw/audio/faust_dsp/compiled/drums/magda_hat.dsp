declare name "MagdaHat";
declare description "Synthetic hi-hat in two layers with independent controls: a metallic Ring (inharmonic additive partials with a Spread/dissonance control and a decay Curve) and a high-passed Noise sizzle (with its own decay Curve), each with its own level and decay. Short decays = closed hat, long = open. Knob-tuned; the played MIDI note only gates the voice.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls (see magda_kick.dsp)
// ============================================================================
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 1, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls ([idx:N]) - grouped Ring / Noise.
// Time controls are in milliseconds (* 0.001 -> the seconds en.ar expects).
// Curve is a bipolar -50..50 decay-shape knob (8^(-c/50): 0 linear, + swelled, - fast).
// ============================================================================
// Ring (inharmonic additive partials)
ringLvl    = hslider("Ring [idx:0]",       0.6,  0.0, 1.0,  0.001);
ringPitch  = hslider("Pitch [idx:1]",      540,  200, 2000, 1);
spread     = hslider("Spread [idx:2]",     1.0,  0.5, 2.0,  0.001);
ringDec    = hslider("Ring Decay [idx:3]", 10,   0.01, 2000, 0.01) * 0.001;
// Noise
noiseLvl   = hslider("Noise [idx:4]",       0.5,  0.0, 1.0,   0.001);
hpFreq     = hslider("HP Freq [idx:5]",     8000, 800, 18000, 1);
noiseDec   = hslider("Noise Decay [idx:6]", 100,  5,   2000,  1) * 0.001;
// Decay curves
ringCurve  = hslider("Ring Curve [idx:7]",  0,   -50,  50,    1);
noiseCurve = hslider("Noise Curve [idx:8]", 0,   -50,  50,    1);
// Noise high-pass shaping: resonance turns the fixed Butterworth HP into a
// resonant high-pass, Sat adds a touch of saturation. Both 0..1, 0 = neutral.
hpReso     = hslider("HP Reso [idx:9]",     0.0,  0.0, 1.0,   0.001);
noiseSat   = hslider("Sat [idx:10]",        0.0,  0.0, 1.0,   0.001);

// ============================================================================
// Voice: metallic Ring + Noise sizzle.
// ============================================================================
// Inharmonic additive ring (manual sum so the decay is curve-shapeable). Spread
// scales each partial's deviation from the fundamental: 1 = nominal metallic,
// >1 more dissonant, <1 toward harmonic/bell. Higher partials decay faster.
sr(b)  = 1.0 + (b - 1.0) * spread;
ratios = (sr(1.0), sr(1.34), sr(1.81), sr(2.27), sr(2.67), sr(3.08));
gains  = (1.0, 0.8, 0.7, 0.6, 0.5, 0.45);
N      = 6;
ringCurveExp = pow(8.0, -ringCurve / 50.0);
partial(i) = os.osc(ringPitch * ba.take(i + 1, ratios)) * ba.take(i + 1, gains) *
             pow(en.ar(0.001, ringDec * (1.0 - 0.5 * (i / N)), gate), ringCurveExp);
ring = par(i, N, partial(i)) :> _ : *(ringLvl);

// High-passed noise sizzle with its own curve-shaped decay. HP Reso maps 0..1 ->
// Q 0.5..10 on a resonant high-pass; Sat is a dry/wet blend into tanh (identity
// at 0, normalized drive at 1) so a little adds grit without changing level.
noiseCurveExp = pow(8.0, -noiseCurve / 50.0);
hpQ  = 0.5 + hpReso * 9.5;
satK = 4.0;
sat(x) = (1.0 - noiseSat) * x + noiseSat * (ma.tanh(x * satK) / ma.tanh(satK));
noise = ((no.noise : fi.resonhp(hpFreq, hpQ, 1.0)) : sat) *
        pow(en.ar(0.001, noiseDec, gate), noiseCurveExp) * noiseLvl;

voice   = ma.tanh(ring + noise) * gain;
process = voice <: _, _;
