declare name "MagdaPolySynth";
declare description "Four-oscillator polyphonic synth: four detunable oscillators (sine / saw / square / triangle), up to eight stereo unison copies of them, into two multimode state-variable filters (serial or parallel) sharing an envelope, plus an ADSR amplitude envelope.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");
msm = library("magda_smoothing.lib");
svf = library("magda_svf.lib");

// ============================================================================
// Reserved per-voice MIDI controls
// ============================================================================
// These three labels are the Faust polyphony convention. The C++ wrapper's
// mydsp_poly voice allocator drives them per voice from note number, velocity
// and note-on/off - they carry NO [idx] annotation, so they are never exposed
// as host macro slots.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
// Pitch-bend wheel, normalised to [-1, 1]. Like freq/gain/gate this carries no
// [idx] (the wrapper drives it from MIDI pitch-wheel, per voice), but unlike
// those it is not a Faust-reserved name, so the engine never touches it - we own
// it entirely. Scaled by the user's Bend Range (semitones) below.
bend = hslider("bend", 0, -1, 1, 0.001);

// ============================================================================
// Host macro controls
// ============================================================================
// Pinned to stable [idx:N] slots and harvested by the wrapper. group=false
// polyphony gives each voice its own copy of these zones; the wrapper fans a
// single host value out to every voice each block. Slots are laid out four per
// oscillator (wave / level / coarse / fine) so the C++ slot constants map as
// osc n -> base 4*(n-1); the filter section follows at idx 16+ and the amp
// envelope at idx 24+.
// Keyed on the gate: a recycled voice takes the current control values on
// the note, instead of gliding from the note it last played (#2661).
smoo = msm.polySmooth(gate, ba.tau2pole(0.01));

// Pitch-bend offset in semitones: the normalised wheel scaled by the user's
// Bend Range, smoothed to avoid zipper noise from the 14-bit wheel steps.
bendRange = hslider("Bend Range [unit:st] [idx:30]", 2, 0, 24, 1);
bendSemis = (bend * bendRange) : smoo;

// Portamento: glide the base pitch toward the played note over `glide` seconds.
// 0 = instant. The wrapper forces this zone to 0 on the poly voices, so only the
// Mono/Legato voice glides; poly patches are unaffected.
glide = hslider("Glide [unit:ms] [idx:32]", 0, 0, 2000, 1) / 1000.0;
freqG = freq : msm.smooth(ba.tau2pole(glide));

// Per-oscillator phase reset: each oscillator can independently restart at phase
// 0 on note-on, for consistent/punchy attacks. The reset fires for one sample on
// the gate's rising edge (also the Mono retrigger edge) when that oscillator's
// toggle is on.
gateRise = gate > gate';
osc1Reset = nentry("Osc 1 Reset [idx:33] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
osc2Reset = nentry("Osc 2 Reset [idx:34] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
osc3Reset = nentry("Osc 3 Reset [idx:35] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
osc4Reset = nentry("Osc 4 Reset [idx:36] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);

// Resettable, alias-suppressed oscillators using PolyBLEP. PolyBLEP is stateless,
// so resetting the phase is click-free (unlike DPW, whose differentiator spikes
// when the phase jumps). Built on Faust's reset-capable phasor.
polyblep(dt, t) = select2(t < dt, select2(t > 1.0 - dt, 0.0, near1), near0)
with {
    x0 = t / dt;
    near0 = 2.0 * x0 - x0 * x0 - 1.0;
    x1 = (t - 1.0) / dt;
    near1 = x1 * x1 + 2.0 * x1 + 1.0;
};
// One phasor per oscillator feeds every shape; `ph` is where a reset puts it.
// selectn runs all four shapes; gating them with enable measured slower.
oscShape(wave, f, ph, rst) = ba.selectn(4, int(wave), shape(0), shape(1), shape(2), shape(3))
with {
    p = os.lf_sawpos_phase_reset(f, ph, rst);
    dt = max(ma.EPSILON, f / ma.SR);
    saw = (2.0 * p - 1.0) - polyblep(dt, p);
    sqr = (2.0 * float(p < 0.5) - 1.0) + polyblep(dt, p) - polyblep(dt, ma.frac(p + 0.5));
    shape(0) = sin(2.0 * ma.PI * p);
    shape(1) = saw;
    shape(2) = sqr;
    shape(3) = sqr : fi.pole(0.999) : *(4.0 * f / ma.SR);  // integrated square
};

// One oscillator. `wave` selects the shape (Sine/Saw/Square/Triangle),
// `coarse` shifts pitch in semitones and `fine` in cents; `level` is the
// pre-mix gain in dB (converted to a linear multiplier). `bendSemis` shifts every
// osc together by the live pitch-bend amount; `freqG` carries the glide; `rstOn`
// restarts this oscillator's phase on note-on. `en` (0/1) mutes a disabled
// oscillator, smoothed so toggling it does not click. `cents` and `ph` are the
// unison copy's detune and reset phase.
oscBank(wave, level, coarse, fine, rstOn, en, cents, ph) =
    oscShape(wave, f, ph, (rstOn > 0) * gateRise)
    * (level : ba.db2linear : smoo)
    * (en : smoo)
with {
    f = freqG * pow(2.0, (coarse + (fine + cents) / 100.0 + bendSemis) / 12.0);
};

// Oscillator 1 (idx 0..3)
osc1Wave   = nentry("Osc 1 Wave [idx:0] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc1Level  = hslider("Osc 1 Level [unit:dB] [idx:1]", 0, -60, 6, 0.1);
osc1Coarse = hslider("Osc 1 Coarse [unit:st] [idx:2]", 0, -24, 24, 1);
osc1Fine   = hslider("Osc 1 Fine [unit:cent] [idx:3]", 0, -100, 100, 1);

// Oscillator 2 (idx 4..7)
osc2Wave   = nentry("Osc 2 Wave [idx:4] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc2Level  = hslider("Osc 2 Level [unit:dB] [idx:5]", -60, -60, 6, 0.1);
osc2Coarse = hslider("Osc 2 Coarse [unit:st] [idx:6]", 0, -24, 24, 1);
osc2Fine   = hslider("Osc 2 Fine [unit:cent] [idx:7]", 0, -100, 100, 1);

// Oscillator 3 (idx 8..11)
osc3Wave   = nentry("Osc 3 Wave [idx:8] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc3Level  = hslider("Osc 3 Level [unit:dB] [idx:9]", -60, -60, 6, 0.1);
osc3Coarse = hslider("Osc 3 Coarse [unit:st] [idx:10]", 0, -24, 24, 1);
osc3Fine   = hslider("Osc 3 Fine [unit:cent] [idx:11]", 0, -100, 100, 1);

// Oscillator 4 (idx 12..15)
osc4Wave   = nentry("Osc 4 Wave [idx:12] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc4Level  = hslider("Osc 4 Level [unit:dB] [idx:13]", -60, -60, 6, 0.1);
osc4Coarse = hslider("Osc 4 Coarse [unit:st] [idx:14]", 0, -24, 24, 1);
osc4Fine   = hslider("Osc 4 Fine [unit:cent] [idx:15]", 0, -100, 100, 1);

// Filter section (idx 16..23): multimode SVF with a dedicated ADSR that
// modulates the cutoff by +/- `Filter Env` octaves.
filterType = nentry("Filter Type [idx:16] [style:menu{'Lowpass':0;'Highpass':1;'Bandpass':2;'Notch':3}]", 0, 0, 3, 1);
cutoff  = hslider("Cutoff [unit:Hz] [idx:17] [scale:log]", 3000, 50, 18000, 1);
res     = hslider("Resonance [idx:18]", 0.3, 0.0, 0.95, 0.001);
fEnvAmt = hslider("Filter Env [unit:oct] [idx:19]", 0, -4, 4, 0.01);
// Pre-filter drive saturation, ported verbatim from magda_filter_svf.dsp.
// idx:28 (after the amp envelope) so the existing slot indices stay stable.
fDrive  = hslider("Filter Drive [idx:28]", 0.0, 0.0, 1.0, 0.001) : smoo;
// Poles: 2, 4 or 6. Named Slope for its stable slot id.
fSlope  = nentry("Filter Slope [idx:29] [style:menu{'2':0;'4':1;'6':2}]", 0, 0, 2, 1);
// Envelope times are exposed in milliseconds for finer control; the host
// formatter shows ms below 1 s and switches to seconds above. The DSP converts
// back to seconds at the envelope.
fAtt    = hslider("Filter Attack [unit:ms] [idx:20]", 5, 1, 2000, 1) / 1000.0;
fDec    = hslider("Filter Decay [unit:ms] [idx:21]", 200, 1, 2000, 1) / 1000.0;
fSus    = hslider("Filter Sustain [idx:22]", 0.7, 0.0, 1.0, 0.001);
fRel    = hslider("Filter Release [unit:ms] [idx:23]", 400, 1, 4000, 1) / 1000.0;

// Amp envelope (idx 24..27)
aAtt = hslider("Amp Attack [unit:ms] [idx:24]", 5, 1, 2000, 1) / 1000.0;
aDec = hslider("Amp Decay [unit:ms] [idx:25]", 200, 1, 2000, 1) / 1000.0;
aSus = hslider("Amp Sustain [idx:26]", 0.7, 0.0, 1.0, 0.001);
aRel = hslider("Amp Release [unit:ms] [idx:27]", 400, 1, 4000, 1) / 1000.0;

// Velocity routing. The per-voice `gain` zone carries note velocity (0..1).
// `Vel Amp` sets how much velocity scales loudness (0 = ignore velocity, all
// notes full; 1 = full velocity range). `Vel Filter` opens the cutoff by up to
// that many octaves at full velocity. Smoothed so the per-note velocity step
// does not click.
velAmp  = hslider("Vel Amp [idx:37]", 1.0, 0.0, 1.0, 0.001) : smoo;
velFilt = hslider("Vel Filter [unit:oct] [idx:38]", 0.0, 0.0, 6.0, 0.01) : smoo;
ampVel  = ((1.0 - velAmp) + velAmp * gain) : smoo;

// Per-oscillator enable (idx 39..42): an On/Off toggle that mutes that
// oscillator. Default On so existing patches are unchanged.
osc1Enable = nentry("Osc 1 Enable [idx:39] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
osc2Enable = nentry("Osc 2 Enable [idx:40] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
osc3Enable = nentry("Osc 3 Enable [idx:41] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
osc4Enable = nentry("Osc 4 Enable [idx:42] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);

// Output gain (idx 43): a master trim applied per voice after the soft-clip, so
// thick multi-oscillator / polyphonic patches (which can sum hot) can be pulled
// back down. 0 dB = unity.
outGain = hslider("Output [unit:dB] [idx:43]", 0, -60, 6, 0.1) : ba.db2linear : smoo;

// Filter 2 (idx 44..49) mirrors Filter 1 and shares its envelope and Vel Filter.
f2Type    = nentry("Filter 2 Type [idx:44] [style:menu{'Lowpass':0;'Highpass':1;'Bandpass':2;'Notch':3}]", 0, 0, 3, 1);
f2Cutoff  = hslider("Filter 2 Cutoff [unit:Hz] [idx:45] [scale:log]", 3000, 50, 18000, 1);
f2Res     = hslider("Filter 2 Resonance [idx:46]", 0.3, 0.0, 0.95, 0.001);
f2EnvAmt  = hslider("Filter 2 Env [unit:oct] [idx:47]", 0, -4, 4, 0.01);
f2Drive   = hslider("Filter 2 Drive [idx:48]", 0.0, 0.0, 1.0, 0.001) : smoo;
f2Slope   = nentry("Filter 2 Slope [idx:49] [style:menu{'2':0;'4':1;'6':2}]", 0, 0, 2, 1);

// A bypassed filter passes its input through. Filter 2 starts bypassed so
// existing patches keep their sound.
f1On      = nentry("Filter 1 Enable [idx:50] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);
f2On      = nentry("Filter 2 Enable [idx:51] [style:menu{'Off':0;'On':1}]", 0, 0, 1, 1);
// Serial: Filter 1 into Filter 2. Parallel: both fed the osc mix, mixed by Blend
// (0 = Filter 1 only, 1 = Filter 2 only).
fRouting  = nentry("Filter Routing [idx:52] [style:menu{'Serial':0;'Parallel':1}]", 0, 0, 1, 1);
fBlend    = hslider("Filter Blend [idx:53]", 0.5, 0.0, 1.0, 0.001) : smoo;

// Unison (idx 54..56). One voice by default, which is the synth without unison.
uVoices   = nentry("Unison Voices [idx:54] [style:menu{'1':0;'2':1;'4':2;'8':3}]", 0, 0, 3, 1);
uDetune   = hslider("Unison Detune [unit:cent] [idx:55]", 20, 0, 100, 0.1) : smoo;
uSpread   = hslider("Unison Spread [idx:56]", 0.5, 0.0, 1.0, 0.001) : smoo;

// ============================================================================
// DSP
// ============================================================================
oscMix(cents, ph) = oscBank(osc1Wave, osc1Level, osc1Coarse, osc1Fine, osc1Reset, osc1Enable, cents, ph)
                  + oscBank(osc2Wave, osc2Level, osc2Coarse, osc2Fine, osc2Reset, osc2Enable, cents, ph)
                  + oscBank(osc3Wave, osc3Level, osc3Coarse, osc3Fine, osc3Reset, osc3Enable, cents, ph)
                  + oscBank(osc4Wave, osc4Level, osc4Coarse, osc4Fine, osc4Reset, osc4Enable, cents, ph);

// Unison (#2924): copies spaced evenly across +/- Detune, panned to alternate
// sides as 4OSC does, wider towards the edges, summed at 1/sqrt(n). Copies past
// the count don't run. One copy is the synth without unison, sample for sample.
uMax = 8;
uCount = ba.selectn(4, int(uVoices), 1, 2, 4, 8);
uPos(k) = select2(uCount > 1, 0.0, 2.0 * k / max(1.0, uCount - 1.0) - 1.0);
uPan(k) = uSpread * (1.0 - 2.0 * (k % 2)) * abs(uPos(k));
uPhase(k) = ma.frac(k * 0.6180339887);  // spread the copies' reset phases
uCopy(0) = oscMix(0.0, 0.0);
uCopy(k) = enable(oscMix(uDetune * uPos(k), uPhase(k)), uCount > k) * (uCount > k);
unisonMix = par(k, uMax, uCopy(k) <: *(1.0 - uPan(k)), *(1.0 + uPan(k))) :> /(sqrt(uCount)), /(sqrt(uCount));

// Resonance 0..0.95 -> Q 0.5..~9.5. Filter-envelope output scales the cutoff
// exponentially (in octaves) and the result is clamped to the audio band.
// en.adsre (exponential segments), not en.adsr: the linear adsr's attack
// counter hard-resets on the gate's rising edge, so a Mono retrigger (the
// host's one-sample gate dip) collapses the envelope from sustain to zero in
// one sample - an audible click on every mono note. adsre is a one-pole slew
// toward the segment target, so it resumes from the current level instead.
filterEnv = en.adsre(fAtt, fDec, fSus, fRel, gate);
fcOf(cut, envAmt) = (cut * pow(2.0, envAmt * filterEnv + velFilt * gain)) : max(20.0) : min(20000.0) : smoo;

// The SVF with 1-3 stages, resonance in the last only, as magda_filter_svf_staged.dsp.
// A single stage is fi.svf exactly (k = 1/Q, unscaled band-pass).
kFlat = sqrt(2.0);
svfPoles(type, poles, fc, q, x) = ba.selectn(3, int(poles), x, pre1, pre2) : svf.stage(m, g, 1.0 / q, 1.0)
with {
    m = ba.selectn(4, int(type), 0, 2, 1, 3);  // menu order -> magda_svf.lib's
    g = tan(fc * ma.PI / ma.SR);
    pre1 = x : svf.stage(m, g, kFlat, kFlat);
    pre2 = pre1 : svf.stage(m, g, kFlat, kFlat);
};

// Gain staging. The SVF's resonant peak adds up to ~Q of gain near the cutoff,
// so trim the filter input as resonance rises to keep the level roughly
// constant (filterBlock). The per-voice tanh then soft-clips peaks so a hot voice
// (high velocity / resonance) overdrives gracefully instead of hard-clipping
// downstream - tanh is ~linear for small signals, so clean patches stay clean.
ampEnv  = en.adsre(aAtt, aDec, aSus, aRel, gate);  // see filterEnv: no click on Mono retrigger
// Drive: dry/saturated lerp; tanh(4) normalisation keeps unity-amplitude
// signals at unity at full drive (matches magda_filter_svf.dsp).
drivenIn(d, x) = (1.0 - d) * x
               + d * (ma.tanh(4.0 * x) / ma.tanh(4.0));

filterBlock(on, type, poles, cut, envAmt, r, d, x) = x * (1.0 - e) + y * e
with {
    e = on : smoo;
    y = x * (1.0 - 0.5 * r) : drivenIn(d) : svfPoles(type, poles, fcOf(cut, envAmt), 0.5 + r * 9.5);
};
filter1 = filterBlock(f1On, filterType, fSlope, cutoff, fEnvAmt, res, fDrive);
filter2 = filterBlock(f2On, f2Type, f2Slope, f2Cutoff, f2EnvAmt, f2Res, f2Drive);

// One Filter 2 serves both routings: its input crossfades from Filter 1's
// output (serial) to the osc mix (parallel).
filters(x) = y1 * r * (1.0 - fBlend) + filter2(y1 * (1.0 - r) + x * r) * (1.0 - r + r * fBlend)
with {
    r = fRouting : smoo;
    y1 = filter1(x);
};

voice(x) = ((x : filters) * ampEnv * ampVel : ma.tanh) * outGain;

// The right channel gets its own filters only once unison can pan.
stereo = uCount > 1;
process = unisonMix : (voice, enable(voice, stereo)) : (_ <: _, _), _ : _, select2(stereo);
