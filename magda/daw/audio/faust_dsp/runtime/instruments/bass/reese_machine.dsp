import("stdfaust.lib");
declare name "Reese Machine";
declare description "Three detuned saws driven through two notch-and-clip stages, over a clean sine sub. The beating between the outer saws is the Reese; the notches carve it hollow and the clippers give it teeth.";
declare author "MAGDA";
declare license "GPL-3.0";
declare version "1.0";

// Reserved per-voice controls: the polyphonic allocator drives these from MIDI
// note / velocity / gate, so they are never exposed as pool parameters.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// Every continuous control is a per-voice zone the host writes from the message
// thread, so each one is smoothed before it reaches a filter coefficient or a
// gain. Without this a knob drag steps the notch frequency once per block.
smoothed = si.smooth(ba.tau2pole(0.02));

// ============================================================================
// Osc
// ============================================================================

// Three saws: the note itself, plus the outer pair detuned symmetrically above
// and below it. The slow beating between those two is the whole Reese sound, so
// Detune is the patch's primary control and small values are the useful ones -
// past roughly 30 cents it stops beating and starts sounding like a chord.
//
// Two outputs: the saw stack, and a sine one octave down. They are kept apart
// here because they take different routes downstream - see `voice`.
oscSection = vgroup("Osc", saws, sub)
with {
    detune = hslider("Detune [unit:ct] [idx:0]", 14, 0, 50, 0.1) : smoothed;
    // Cents to a frequency ratio. Symmetric in pitch, not in Hz, so the
    // beating rate tracks the note rather than drifting across the keyboard.
    ratio = pow(2.0, detune / 1200.0);
    subLevel = hslider("Sub [idx:14]", 0.5, 0.0, 1.0, 0.001) : smoothed;

    saws = (os.sawtooth(freq * ratio) +
            os.sawtooth(freq) +
            os.sawtooth(freq / ratio)) / 3.0;
    // A sine, not a saw: the sub is there for weight, and any harmonics it
    // carried would collide with the saw stack an octave up rather than
    // reinforce it.
    sub = os.osc(freq * 0.5) * subLevel;
};

// ============================================================================
// Stages
// ============================================================================

// Notch, then soft clip - twice. Ordering matters both times: the notch feeds
// the clipper, so it decides which part of the spectrum gets driven hardest,
// and the second notch then carves the harmonics the first clipper generated.
// Two stages in series is what separates this from a detuned saw patch.
//
// Width is the notch's approximate -3 dB width in Hz (fi.notchw), not a Q, so
// a narrow setting stays narrow as Freq sweeps up.
//
// Bias offsets the signal before the nonlinearity, which adds even harmonics
// and asymmetry. cubicnl_nodc rather than cubicnl: the offset would otherwise
// leave a DC component behind, and two stages of it would compound.
stage1(x) = vgroup("Stage 1", x : fi.notchw(width, nfreq) : ef.cubicnl_nodc(drive, bias))
with {
    nfreq = hslider("Freq [unit:Hz] [scale:log] [idx:1]", 300, 40, 5000, 1) : smoothed;
    width = hslider("Width [unit:Hz] [scale:log] [idx:2]", 200, 10, 2000, 1) : smoothed;
    drive = hslider("Drive [idx:3]", 0.4, 0, 1, 0.001) : smoothed;
    bias = hslider("Bias [idx:4]", 0.0, 0.0, 0.5, 0.001) : smoothed;
};

stage2(x) = vgroup("Stage 2", x : fi.notchw(width, nfreq) : ef.cubicnl_nodc(drive, bias))
with {
    nfreq = hslider("Freq [unit:Hz] [scale:log] [idx:5]", 900, 40, 5000, 1) : smoothed;
    width = hslider("Width [unit:Hz] [scale:log] [idx:6]", 400, 10, 2000, 1) : smoothed;
    drive = hslider("Drive [idx:7]", 0.3, 0, 1, 0.001) : smoothed;
    bias = hslider("Bias [idx:8]", 0.0, 0.0, 0.5, 0.001) : smoothed;
};

// ============================================================================
// Env / Out
// ============================================================================

envSection = vgroup("Env", en.adsr(att, dec, sus, rel, gate))
with {
    att = hslider("Attack [unit:s] [scale:log] [scaleAnchor:0.05] [idx:9]", 0.005, 0.001, 2, 0.001);
    dec = hslider("Decay [unit:s] [scale:log] [scaleAnchor:0.2] [idx:10]", 0.15, 0.001, 4, 0.001);
    sus = hslider("Sustain [idx:11]", 0.8, 0, 1, 0.01);
    rel = hslider("Release [unit:s] [scale:log] [scaleAnchor:0.3] [idx:12]", 0.3, 0.001, 4, 0.001);
};

// Both clip stages add level, and Drive at the top of its range roughly doubles
// it, so the patch needs a trim to stay useful without re-gaining downstream.
// Default is -6 dB to leave headroom for sixteen voices.
outSection(x) = vgroup("Out", x * ba.db2linear(level))
with {
    level = hslider("Output [unit:dB] [idx:13]", -6, -24, 12, 0.1) : smoothed;
};

// The envelope and velocity hit both oscillator paths before the stages, so
// harder playing drives the clippers harder - the level and the amount of dirt
// move together the way they do on hardware.
amp = envSection * gain;

// The saws take the notch/clip chain; the sub goes around it and rejoins at the
// output. Two clippers in series would flatten a sine into something closer to
// a square and hand its energy to harmonics an octave up, which is exactly the
// weight the sub is there to provide. Route it through the stages instead by
// changing this line to `: stage1 : stage2` after the merge.
//
// Mono voice fanned to stereo: the beating is an amplitude effect within the
// single summed signal, so it survives the fan intact.
voice = oscSection : *(amp), *(amp) : (stage1 : stage2), _ :> outSection;
process = voice <: _, _;
