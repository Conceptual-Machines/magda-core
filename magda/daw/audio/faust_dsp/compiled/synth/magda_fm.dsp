declare name "MagdaFM";
declare description "Four-operator FM synth with a full 4x4 modulation matrix: every operator can phase-modulate any operator (the diagonal is self-feedback), so it covers every DX-style algorithm and everything between. Per-op ratio + output level, a shared amp ADSR. The per-voice freq/gain/gate are driven by the poly allocator; all matrix/ratio/level/envelope controls are host macros.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
msm = library("magda_smoothing.lib");
mf = library("magda_filters.lib");

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
// Keep a 0.5 ms floor even when Glide is off. In Mono mode the same oscillator
// keeps sounding across note-ons, and an instantaneous frequency step excites
// the FM feedback loop as a sharp attack transient. The sub-millisecond floor
// is perceptually immediate but removes that discontinuity. The host forces
// Glide to 0 on poly voices, so they receive only this anti-click floor.
glide = hslider("Glide [unit:ms] [idx:32]", 0, 0, 2000, 1) / 1000.0;
freqG = freq : msm.smooth(ba.tau2pole(max(0.0005, glide)));

// Velocity -> amplitude depth. 0 = ignore velocity (every note full level); 1 =
// full velocity range. The per-voice `gain` zone carries the note velocity.
velAmt  = hslider("Vel Amount [idx:33]", 1.0, 0.0, 1.0, 0.001);
gainG = gain : msm.smooth(ba.tau2pole(0.0005));
velGain = (1.0 - velAmt) + velAmt * gainG;

// Per-operator phase reset: restart that operator's phasor at 0 on note-on (a
// punchy, repeatable attack) when enabled. Fires for one sample on the gate's
// rising edge.
gateRise = gate > gate';
reset(0) = nentry("Op1 Reset [idx:34] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
reset(1) = nentry("Op2 Reset [idx:35] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
reset(2) = nentry("Op3 Reset [idx:36] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
reset(3) = nentry("Op4 Reset [idx:37] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);

// en.adsre (exponential segments), not en.adsr: the linear adsr's attack
// counter hard-resets on the gate's rising edge, so a Mono retrigger (the
// host's one-sample gate dip) collapses the envelope from sustain to zero in
// one sample - an audible click on every mono note. adsre is a one-pole slew
// toward the segment target, so it resumes from the current level instead.
env = en.adsre(ampA, ampD, ampS, ampR, gate);

// Restarting a phasor while a reused mono voice is already audible necessarily
// jumps to an unrelated waveform value. Honour Phase Reset when the voice is
// silent (including every fresh poly voice), but retain phase on overlapping
// mono retriggers. At this level a reset discontinuity is inaudible.
phaseResetRise = gateRise * (env' < 0.001);

// Per-operator enable (idx 38..41): an On/Off toggle that mutes the operator
// entirely - it stops sounding AND stops modulating (its output is gated before
// the feedback bus). Smoothed so toggling does not click. Default On.
// (`enable` is a reserved Faust primitive, so the function is named opEn.)
opEn(0) = nentry("Op1 Enable [idx:38] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
opEn(1) = nentry("Op2 Enable [idx:39] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
opEn(2) = nentry("Op3 Enable [idx:40] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
opEn(3) = nentry("Op4 Enable [idx:41] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);

// Filter (#3120). 42 and 43 are the wrapper's Gain and Voice Mode. Off by
// default, so existing patches sound the same. Type is the SVF's, Comb Mode
// the comb's; Poles apply to the SVF (the ladder is 4-pole).
fOn     = nentry("Filter Enable [idx:44] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
fEngine = nentry("Filter Engine [idx:45] [style:menu{'SVF':0;'Ladder':1;'Comb':2}]", 0, 0, 2, 1);
fType   = nentry("Filter Type [idx:46] [style:menu{'Lowpass':0;'Highpass':1;'Bandpass':2;'Notch':3}]", 0, 0, 3, 1);
fComb   = nentry("Filter Comb Mode [idx:47] [style:menu{'+':0;'-':1}]", 0, 0, 1, 1);
fPoles  = nentry("Filter Poles [idx:48] [style:menu{'2':0;'4':1;'6':2}]", 0, 0, 2, 1);
fCutoff = hslider("Filter Cutoff [unit:Hz] [scale:log] [idx:49]", 2000, 20, 20000, 1);
fRes    = hslider("Filter Resonance [idx:50]", 0.3, 0.0, 1.0, 0.001);
fDrive  = hslider("Filter Drive [idx:51]", 0.0, 0.0, 1.0, 0.001);
// Octaves of cutoff per octave of the played note, around C4.
fKey    = hslider("Filter Key Track [idx:52]", 0.0, 0.0, 1.0, 0.001);
fVel    = hslider("Filter Vel [unit:oct] [idx:53]", 0.0, 0.0, 6.0, 0.01);
fEnvAmt = hslider("Filter Env [unit:oct] [idx:54]", 0.0, -4.0, 4.0, 0.01);
fAtt    = hslider("Filter Attack [idx:55]",  5.0,   1.0, 2000.0, 0.1) * 0.001;
fDec    = hslider("Filter Decay [idx:56]",   300.0, 1.0, 2000.0, 0.1) * 0.001;
fSus    = hslider("Filter Sustain [idx:57]", 0.5,   0.0, 1.0,    0.001);
fRel    = hslider("Filter Release [idx:58]", 400.0, 1.0, 4000.0, 0.1) * 0.001;

// The filter as a matrix node (#3121). In Matrix position its input is the
// operators' sends instead of the mix, its output phase-modulates the operators
// through the returns (radians, as the matrix) and reaches the mix at F Level.
fSend(0) = hslider("M 1>F [idx:59]", 0.0, 0.0, 1.0, 0.001);
fSend(1) = hslider("M 2>F [idx:60]", 0.0, 0.0, 1.0, 0.001);
fSend(2) = hslider("M 3>F [idx:61]", 0.0, 0.0, 1.0, 0.001);
fSend(3) = hslider("M 4>F [idx:62]", 0.0, 0.0, 1.0, 0.001);
fRet(0)  = hslider("M F>1 [idx:63]", 0.0, 0.0, 8.0, 0.001);
fRet(1)  = hslider("M F>2 [idx:64]", 0.0, 0.0, 8.0, 0.001);
fRet(2)  = hslider("M F>3 [idx:65]", 0.0, 0.0, 8.0, 0.001);
fRet(3)  = hslider("M F>4 [idx:66]", 0.0, 0.0, 8.0, 0.001);
fLvl     = hslider("F Level [unit:dB] [idx:67]", 0.0, -60.0, 6.0, 0.1);
fPos     = nentry("Filter Position [idx:68] [style:menu{'Output':0;'Matrix':1}]", 0, 0, 1, 1);

// Smooth the continuous controls so dragging a matrix amount / ratio / level
// glides instead of stepping per block (the stepped values were the zipper noise
// on tweak). Wave is discrete and the envelope times are per-note, so they are
// left unsmoothed.
// Gate-keyed: see magda_polysynth. freqG and gainG stay time-smoothed -
// glide is the point of one and the other is already half a millisecond.
sm(x) = x : msm.polySmoo(gate);

// ============================================================================
// Filter
// ============================================================================
// One control-rate selector picks @p dry or an engine, and only that one runs:
// smoothing the on/off switch measured twice the idle cost.
fSel(on) = on * (int(fEngine) + 1);
filter(sel, dry, x) = ba.selectn(4, sel, dry, enable(svfOut, sel == 1),
                                 enable(ladderOut, sel == 2), enable(combOut, sel == 3))
with {
    r = sm(fRes);
    fEnv = en.adsre(fAtt, fDec, fSus, fRel, gate);
    octs = sm(fEnvAmt) * fEnv + sm(fVel) * gainG + sm(fKey) * log(freqG / 261.63) / log(2.0);
    fc = sm(fCutoff) * pow(2.0, octs) : max(20.0) : min(min(20000.0, ma.SR * 0.45));
    driven = x : mf.drive(sm(fDrive));
    // Resonance as the Filter device's: SVF Q 0.5..40, ladder q to 0.99, comb feedback to 0.98.
    svfOut = driven : mf.svfPoles(ba.selectn(4, int(fType), 0, 2, 1, 3), int(fPoles),
                                  tan(fc * ma.PI / ma.SR), 1.0 / (0.5 * pow(80.0, r)));
    ladderOut = driven : mf.ladder(r * 0.99, fc);
    combOut = driven : mf.comb(8192, max(2.0, min(8188.0, ma.SR / fc)),
                               1.0 - 2.0 * (fComb > 0.5), 0.98 * r);
};

// One filter serves both positions. Output filters the operators' mix (#3120);
// Matrix makes it a fifth node, run only while something is sent to it.
inMatrix = fPos > 0.5;
anySend = (fSend(0) + fSend(1) + fSend(2) + fSend(3)) > 0.0;
nodeOn = (fOn > 0.5) * (anySend | (1 - inMatrix));
matrixOn = (fOn > 0.5) * inMatrix * anySend;

// dB -> linear gain (floored to silence at -60 dB), smoothed to avoid steps.
dbGain(db) = sm(db > -59.5) * ba.db2linear(sm(db));
lvl(i) = dbGain(outLvl(i));

// ============================================================================
// FM matrix
// ============================================================================
// Each operator reads its waveform at a phase modulated by the matrix-weighted
// sum of the (one-sample-delayed) operator outputs. `~ si.bus(5)` closes the
// loop, giving the single-sample delay every FM feedback path needs. Phase
// modulation is applied as cycles (pm radians / 2pi) added to the phasor, so it
// works uniformly for every waveshape; Noise ignores phase.
// The filter node is the loop's fifth signal, so it too reads the operators one
// sample late. tanh bounds its input, so a resonant filter in the loop can't
// run away.
operators(y0, y1, y2, y3, yf) = op(0), op(1), op(2), op(3), node, out
with {
    pm(i) = sm(m(0, i)) * y0 + sm(m(1, i)) * y1 + sm(m(2, i)) * y2 + sm(m(3, i)) * y3
          + enable(sm(fRet(i)) * yf, matrixOn);
    // Resettable phasor (os.hs_phasor) so Phase Reset restarts the op at 0 on
    // note-on; glide-smoothed pitch (freqG) carries portamento.
    mphase(i) = os.hs_phasor(1.0, freqG * sm(ratio(i)), (reset(i) > 0) * phaseResetRise) +
                pm(i) / (2.0 * ma.PI);
    p(i) = mphase(i) - floor(mphase(i));  // wrap to [0,1)
    sine(x) = sin(2.0 * ma.PI * x);
    tri(x) = 4.0 * abs(x - 0.5) - 1.0;
    saw(x) = 2.0 * x - 1.0;
    sqr(x) = 2.0 * float(x < 0.5) - 1.0;
    op(i) = ba.selectn(5, int(wave(i)), sine(p(i)), tri(p(i)), saw(p(i)), sqr(p(i)), no.noise)
            * sm(opEn(i));
    ops = op(0) * lvl(0) + op(1) * lvl(1) + op(2) * lvl(2) + op(3) * lvl(3);
    sends = sm(fSend(0)) * y0 + sm(fSend(1)) * y1 + sm(fSend(2)) * y2 + sm(fSend(3)) * y3;
    // The node is the loop's fifth signal; `out` is the voice, after the loop.
    node = filter(fSel(nodeOn), select2(inMatrix, ops, 0.0),
                  select2(inMatrix, ops, enable(ma.tanh(sends), matrixOn)));
    out = select2(inMatrix, node, ops + enable(node * dbGain(fLvl), matrixOn));
};

voice = (operators ~ si.bus(5)) : (si.block(5), _) : *(env) : *(velGain);

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process = voice <: _, _;
