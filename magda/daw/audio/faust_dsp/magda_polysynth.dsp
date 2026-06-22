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

// One oscillator. `wave` selects the shape (Sine/Saw/Square/Triangle),
// `coarse` shifts pitch in semitones and `fine` in cents; `level` is the
// pre-mix gain. selectn evaluates every branch, so all four shapes always run.
oscBank(wave, level, coarse, fine) =
    ba.selectn(4, int(wave), os.osc(f), os.sawtooth(f), os.square(f), os.triangle(f))
    * (level : smoo)
with {
    f = freq * pow(2.0, (coarse + fine / 100.0) / 12.0);
};

// Oscillator 1 (idx 0..3)
osc1Wave   = nentry("Osc 1 Wave [idx:0] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc1Level  = hslider("Osc 1 Level [idx:1]", 0.8, 0.0, 1.0, 0.001);
osc1Coarse = hslider("Osc 1 Coarse [unit:st] [idx:2]", 0, -24, 24, 1);
osc1Fine   = hslider("Osc 1 Fine [unit:cent] [idx:3]", 0, -100, 100, 1);

// Oscillator 2 (idx 4..7)
osc2Wave   = nentry("Osc 2 Wave [idx:4] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc2Level  = hslider("Osc 2 Level [idx:5]", 0.0, 0.0, 1.0, 0.001);
osc2Coarse = hslider("Osc 2 Coarse [unit:st] [idx:6]", 0, -24, 24, 1);
osc2Fine   = hslider("Osc 2 Fine [unit:cent] [idx:7]", 0, -100, 100, 1);

// Oscillator 3 (idx 8..11)
osc3Wave   = nentry("Osc 3 Wave [idx:8] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc3Level  = hslider("Osc 3 Level [idx:9]", 0.0, 0.0, 1.0, 0.001);
osc3Coarse = hslider("Osc 3 Coarse [unit:st] [idx:10]", 0, -24, 24, 1);
osc3Fine   = hslider("Osc 3 Fine [unit:cent] [idx:11]", 0, -100, 100, 1);

// Oscillator 4 (idx 12..15)
osc4Wave   = nentry("Osc 4 Wave [idx:12] [style:menu{'Sine':0;'Saw':1;'Square':2;'Triangle':3}]", 1, 0, 3, 1);
osc4Level  = hslider("Osc 4 Level [idx:13]", 0.0, 0.0, 1.0, 0.001);
osc4Coarse = hslider("Osc 4 Coarse [unit:st] [idx:14]", 0, -24, 24, 1);
osc4Fine   = hslider("Osc 4 Fine [unit:cent] [idx:15]", 0, -100, 100, 1);

// Filter section (idx 16..23): multimode SVF with a dedicated ADSR that
// modulates the cutoff by +/- `Filter Env` octaves.
filterType = nentry("Filter Type [idx:16] [style:menu{'Lowpass':0;'Highpass':1;'Bandpass':2;'Notch':3}]", 0, 0, 3, 1);
cutoff  = hslider("Cutoff [unit:Hz] [idx:17] [scale:log]", 3000, 50, 18000, 1);
res     = hslider("Resonance [idx:18]", 0.3, 0.0, 0.95, 0.001);
fEnvAmt = hslider("Filter Env [unit:oct] [idx:19]", 0, -4, 4, 0.01);
fAtt    = hslider("Filter Attack [unit:s] [idx:20]", 0.005, 0.001, 2.0, 0.001);
fDec    = hslider("Filter Decay [unit:s] [idx:21]", 0.2, 0.001, 2.0, 0.001);
fSus    = hslider("Filter Sustain [idx:22]", 0.7, 0.0, 1.0, 0.001);
fRel    = hslider("Filter Release [unit:s] [idx:23]", 0.4, 0.001, 4.0, 0.001);

// Amp envelope (idx 24..27)
aAtt = hslider("Amp Attack [unit:s] [idx:24]", 0.005, 0.001, 2.0, 0.001);
aDec = hslider("Amp Decay [unit:s] [idx:25]", 0.2, 0.001, 2.0, 0.001);
aSus = hslider("Amp Sustain [idx:26]", 0.7, 0.0, 1.0, 0.001);
aRel = hslider("Amp Release [unit:s] [idx:27]", 0.4, 0.001, 4.0, 0.001);

// ============================================================================
// DSP
// ============================================================================
oscMix = oscBank(osc1Wave, osc1Level, osc1Coarse, osc1Fine)
       + oscBank(osc2Wave, osc2Level, osc2Coarse, osc2Fine)
       + oscBank(osc3Wave, osc3Level, osc3Coarse, osc3Fine)
       + oscBank(osc4Wave, osc4Level, osc4Coarse, osc4Fine);

// Resonance 0..0.95 -> Q 0.5..~9.5. Filter-envelope output scales the cutoff
// exponentially (in octaves) and the result is clamped to the audio band.
Q         = 0.5 + res * 9.5;
filterEnv = en.adsr(fAtt, fDec, fSus, fRel, gate);
fc        = (cutoff * pow(2.0, fEnvAmt * filterEnv)) : max(20.0) : min(20000.0) : smoo;

filterMux(x) = ba.selectn(4, int(filterType),
    x : fi.svf.lp(fc, Q),
    x : fi.svf.hp(fc, Q),
    x : fi.svf.bp(fc, Q),
    x : fi.svf.notch(fc, Q));

ampEnv = en.adsr(aAtt, aDec, aSus, aRel, gate);
voice  = oscMix : filterMux * ampEnv * gain;

// Mono voice fanned to a stereo pair (the poly allocator sums all voices).
process = voice <: _, _;
