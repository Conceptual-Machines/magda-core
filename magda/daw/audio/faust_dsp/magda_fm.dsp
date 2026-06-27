declare name "MagdaFM";
declare description "Four-operator FM synth with a full 4x4 modulation matrix: every operator can phase-modulate any operator (the diagonal is self-feedback), so it covers every DX-style algorithm and everything between. Per-op ratio + output level, a shared amp ADSR. The per-voice freq/gain/gate are driven by the poly allocator; all matrix/ratio/level/envelope controls are host macros.";

import("stdfaust.lib");

// ============================================================================
// Reserved per-voice MIDI controls
// ============================================================================
freq = nentry("freq", 440, 20, 20000, 0.01);
gain = nentry("gain", 0.8, 0, 1, 0.01);
gate = button("gate");

// ============================================================================
// Host macro controls
// ============================================================================
// 4x4 modulation matrix M(j,i) = how much operator j phase-modulates operator i
// (in radians of phase, at the modulator's full swing). Row-major host slots:
// idx = j*4 + i, so the diagonal idx 0/5/10/15 is each operator's self-feedback.
// Default patch: op2 -> op1 (a classic 2-op stack) so a fresh instance sounds.
m(0,0) = hslider("M 1>1 [idx:0]",  0.0, 0.0, 8.0, 0.001);
m(0,1) = hslider("M 1>2 [idx:1]",  0.0, 0.0, 8.0, 0.001);
m(0,2) = hslider("M 1>3 [idx:2]",  0.0, 0.0, 8.0, 0.001);
m(0,3) = hslider("M 1>4 [idx:3]",  0.0, 0.0, 8.0, 0.001);
m(1,0) = hslider("M 2>1 [idx:4]",  2.0, 0.0, 8.0, 0.001);
m(1,1) = hslider("M 2>2 [idx:5]",  0.0, 0.0, 8.0, 0.001);
m(1,2) = hslider("M 2>3 [idx:6]",  0.0, 0.0, 8.0, 0.001);
m(1,3) = hslider("M 2>4 [idx:7]",  0.0, 0.0, 8.0, 0.001);
m(2,0) = hslider("M 3>1 [idx:8]",  0.0, 0.0, 8.0, 0.001);
m(2,1) = hslider("M 3>2 [idx:9]",  0.0, 0.0, 8.0, 0.001);
m(2,2) = hslider("M 3>3 [idx:10]", 0.0, 0.0, 8.0, 0.001);
m(2,3) = hslider("M 3>4 [idx:11]", 0.0, 0.0, 8.0, 0.001);
m(3,0) = hslider("M 4>1 [idx:12]", 0.0, 0.0, 8.0, 0.001);
m(3,1) = hslider("M 4>2 [idx:13]", 0.0, 0.0, 8.0, 0.001);
m(3,2) = hslider("M 4>3 [idx:14]", 0.0, 0.0, 8.0, 0.001);
m(3,3) = hslider("M 4>4 [idx:15]", 0.0, 0.0, 8.0, 0.001);

// Per-operator frequency ratio (multiple of the played note).
ratio(0) = hslider("Op1 Ratio [idx:16]", 1.0, 0.25, 16.0, 0.001);
ratio(1) = hslider("Op2 Ratio [idx:17]", 2.0, 0.25, 16.0, 0.001);
ratio(2) = hslider("Op3 Ratio [idx:18]", 1.0, 0.25, 16.0, 0.001);
ratio(3) = hslider("Op4 Ratio [idx:19]", 1.0, 0.25, 16.0, 0.001);

// Per-operator output level (carrier mix). Default: only op1 reaches the output.
// Output level in dB (carrier mix). -60 dB is the silent floor; the dsp converts
// to a linear gain in `mix`.
outLvl(0) = hslider("Op1 Level [unit:dB] [idx:20]",   0.0, -60.0, 6.0, 0.1);
outLvl(1) = hslider("Op2 Level [unit:dB] [idx:21]", -60.0, -60.0, 6.0, 0.1);
outLvl(2) = hslider("Op3 Level [unit:dB] [idx:22]", -60.0, -60.0, 6.0, 0.1);
outLvl(3) = hslider("Op4 Level [unit:dB] [idx:23]", -60.0, -60.0, 6.0, 0.1);

// Amp envelope (ms; the dsp converts to seconds). Sustain is a level [0,1].
ampA = hslider("Amp Attack [idx:24]",  5.0,   1.0, 2000.0, 0.1) * 0.001;
ampD = hslider("Amp Decay [idx:25]",   300.0, 1.0, 2000.0, 0.1) * 0.001;
ampS = hslider("Amp Sustain [idx:26]", 0.5,   0.0, 1.0,    0.001);
ampR = hslider("Amp Release [idx:27]", 400.0, 1.0, 4000.0, 0.1) * 0.001;

// Per-operator waveform: 0 Sine / 1 Triangle / 2 Saw / 3 Square / 4 Noise.
wave(0) = nentry("Op1 Wave [idx:28]", 0, 0, 4, 1);
wave(1) = nentry("Op2 Wave [idx:29]", 0, 0, 4, 1);
wave(2) = nentry("Op3 Wave [idx:30]", 0, 0, 4, 1);
wave(3) = nentry("Op4 Wave [idx:31]", 0, 0, 4, 1);

// Portamento glide (ms -> seconds): smooth the played pitch toward the target.
// The host forces this to 0 on the poly voices, so only the Mono/Legato voice
// glides; poly patches are unaffected.
glide = hslider("Glide [unit:ms] [idx:32]", 0, 0, 2000, 1) / 1000.0;
freqG = freq : si.smooth(ba.tau2pole(glide));

// Velocity -> amplitude depth. 0 = ignore velocity (every note full level); 1 =
// full velocity range. The per-voice `gain` zone carries the note velocity.
velAmt  = hslider("Vel Amount [idx:33]", 1.0, 0.0, 1.0, 0.001);
velGain = (1.0 - velAmt) + velAmt * gain;

// Per-operator phase reset: restart that operator's phasor at 0 on note-on (a
// punchy, repeatable attack) when enabled. Fires for one sample on the gate's
// rising edge.
gateRise = gate > gate';
reset(0) = nentry("Op1 Reset [idx:34] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
reset(1) = nentry("Op2 Reset [idx:35] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
reset(2) = nentry("Op3 Reset [idx:36] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
reset(3) = nentry("Op4 Reset [idx:37] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);

// Per-operator enable (idx 38..41): an On/Off toggle that mutes the operator
// entirely - it stops sounding AND stops modulating (its output is gated before
// the feedback bus). Smoothed so toggling does not click. Default On.
// (`enable` is a reserved Faust primitive, so the function is named opEn.)
opEn(0) = nentry("Op1 Enable [idx:38] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
opEn(1) = nentry("Op2 Enable [idx:39] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
opEn(2) = nentry("Op3 Enable [idx:40] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
opEn(3) = nentry("Op4 Enable [idx:41] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);

// ============================================================================
// FM matrix
// ============================================================================
// Each operator reads its waveform at a phase modulated by the matrix-weighted
// sum of the (one-sample-delayed) operator outputs. `~ si.bus(4)` closes the
// 4-wide feedback loop, giving the single-sample delay every FM feedback path
// needs. Phase modulation is applied as cycles (pm radians / 2pi) added to the
// phasor, so it works uniformly for every waveshape; Noise ignores phase.
// Smooth the continuous controls so dragging a matrix amount / ratio / level
// glides instead of stepping per block (the stepped values were the zipper noise
// on tweak). Wave is discrete and the envelope times are per-note, so they are
// left unsmoothed.
sm(x) = x : si.smoo;

operators(y0, y1, y2, y3) = op(0), op(1), op(2), op(3)
with {
    pm(i) = sm(m(0, i)) * y0 + sm(m(1, i)) * y1 + sm(m(2, i)) * y2 + sm(m(3, i)) * y3;
    // Resettable phasor (os.hs_phasor) so Phase Reset restarts the op at 0 on
    // note-on; glide-smoothed pitch (freqG) carries portamento.
    mphase(i) = os.hs_phasor(1.0, freqG * sm(ratio(i)), (reset(i) > 0) * gateRise) +
                pm(i) / (2.0 * ma.PI);
    p(i) = mphase(i) - floor(mphase(i));  // wrap to [0,1)
    sine(x) = sin(2.0 * ma.PI * x);
    tri(x) = 4.0 * abs(x - 0.5) - 1.0;
    saw(x) = 2.0 * x - 1.0;
    sqr(x) = 2.0 * float(x < 0.5) - 1.0;
    op(i) = ba.selectn(5, int(wave(i)), sine(p(i)), tri(p(i)), saw(p(i)), sqr(p(i)), no.noise)
            * sm(opEn(i));
};

// dB -> linear gain (floored to silence at -60 dB), smoothed to avoid steps.
lvl(i) = sm(outLvl(i) > -59.5) * ba.db2linear(sm(outLvl(i)));
mix(y0, y1, y2, y3) = y0 * lvl(0) + y1 * lvl(1) + y2 * lvl(2) + y3 * lvl(3);

env = en.adsr(ampA, ampD, ampS, ampR, gate);

voice = (operators ~ si.bus(4)) : mix : *(env) : *(velGain);

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process = voice <: _, _;
