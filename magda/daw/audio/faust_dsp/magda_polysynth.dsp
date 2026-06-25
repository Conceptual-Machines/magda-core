declare name "MagdaPolySynth";
declare description "Four-oscillator polyphonic synth: four detunable oscillators (sine / saw / square / triangle) summed into a multimode state-variable filter with its own envelope, plus an ADSR amplitude envelope.";

import("stdfaust.lib");

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
smoo = si.smooth(ba.tau2pole(0.01));

// Pitch-bend offset in semitones: the normalised wheel scaled by the user's
// Bend Range, smoothed to avoid zipper noise from the 14-bit wheel steps.
bendRange = hslider("Bend Range [unit:st] [idx:30]", 2, 0, 24, 1);
bendSemis = (bend * bendRange) : smoo;

// Portamento: glide the base pitch toward the played note over `glide` seconds.
// 0 = instant. The wrapper forces this zone to 0 on the poly voices, so only the
// Mono/Legato voice glides; poly patches are unaffected.
glide = hslider("Glide [unit:ms] [idx:32]", 0, 0, 2000, 1) / 1000.0;
freqG = freq : si.smooth(ba.tau2pole(glide));

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
sawBlep(f, ph, rst) = (2.0 * p - 1.0) - polyblep(dt, p)
with {
    p = os.lf_sawpos_phase_reset(f, ph, rst);
    dt = max(ma.EPSILON, f / ma.SR);
};
sqrBlep(f, rst) = (2.0 * float(p < 0.5) - 1.0) + polyblep(dt, p) - polyblep(dt, ma.frac(p + 0.5))
with {
    p = os.lf_sawpos_reset(f, rst);
    dt = max(ma.EPSILON, f / ma.SR);
};
triBlep(f, rst) = sqrBlep(f, rst) : fi.pole(0.999) : *(4.0 * f / ma.SR);  // integrated square
sinBlep(f, rst) = sin(2.0 * ma.PI * os.lf_sawpos_reset(f, rst));
oscShape(wave, f, rst) =
    ba.selectn(4, int(wave), sinBlep(f, rst), sawBlep(f, 0.0, rst), sqrBlep(f, rst), triBlep(f, rst));

// One oscillator. `wave` selects the shape (Sine/Saw/Square/Triangle),
// `coarse` shifts pitch in semitones and `fine` in cents; `level` is the
// pre-mix gain in dB (converted to a linear multiplier). selectn evaluates every
// branch, so all four shapes always run. `bendSemis` shifts every osc together by
// the live pitch-bend amount; `freqG` carries the glide; `rstOn` restarts this
// oscillator's phase on note-on.
oscBank(wave, level, coarse, fine, rstOn) =
    oscShape(wave, f, (rstOn > 0) * gateRise)
    * (level : ba.db2linear : smoo)
with {
    f = freqG * pow(2.0, (coarse + fine / 100.0 + bendSemis) / 12.0);
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
// Slope: 12 dB/oct (single 2-pole) or 24 dB/oct (two cascaded stages).
fSlope  = nentry("Filter Slope [idx:29] [style:menu{'12 dB':0;'24 dB':1}]", 0, 0, 1, 1);
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

// ============================================================================
// DSP
// ============================================================================
oscMix = oscBank(osc1Wave, osc1Level, osc1Coarse, osc1Fine, osc1Reset)
       + oscBank(osc2Wave, osc2Level, osc2Coarse, osc2Fine, osc2Reset)
       + oscBank(osc3Wave, osc3Level, osc3Coarse, osc3Fine, osc3Reset)
       + oscBank(osc4Wave, osc4Level, osc4Coarse, osc4Fine, osc4Reset);

// Resonance 0..0.95 -> Q 0.5..~9.5. Filter-envelope output scales the cutoff
// exponentially (in octaves) and the result is clamped to the audio band.
Q         = 0.5 + res * 9.5;
filterEnv = en.adsr(fAtt, fDec, fSus, fRel, gate);
fc        = (cutoff * pow(2.0, fEnvAmt * filterEnv + velFilt * gain)) : max(20.0) : min(20000.0) : smoo;

filterMux(x) = ba.selectn(4, int(filterType),
    x : fi.svf.lp(fc, Q),
    x : fi.svf.hp(fc, Q),
    x : fi.svf.bp(fc, Q),
    x : fi.svf.notch(fc, Q));

// Slope: 12 dB = one 2-pole stage, 24 dB = two cascaded stages. Both branches
// run (selectn is strict); the menu picks one.
filterSlope(x) = (filterMux(x), filterMux(filterMux(x))) : ba.selectn(2, int(fSlope));

// Gain staging. The SVF's resonant peak adds up to ~Q of gain near the cutoff,
// so trim the filter input as resonance rises to keep the level roughly
// constant (resComp). The per-voice tanh then soft-clips peaks so a hot voice
// (high velocity / resonance) overdrives gracefully instead of hard-clipping
// downstream - tanh is ~linear for small signals, so clean patches stay clean.
resComp = 1.0 - 0.5 * res;
ampEnv  = en.adsr(aAtt, aDec, aSus, aRel, gate);
// Drive: dry/saturated lerp; tanh(4) normalisation keeps unity-amplitude
// signals at unity at full drive (matches magda_filter_svf.dsp).
drivenIn(x) = (1.0 - fDrive) * x
            + fDrive * (ma.tanh(4.0 * x) / ma.tanh(4.0));
voice   = (oscMix * resComp : drivenIn : filterSlope) * ampEnv * ampVel : ma.tanh;

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process = voice <: _, _;
