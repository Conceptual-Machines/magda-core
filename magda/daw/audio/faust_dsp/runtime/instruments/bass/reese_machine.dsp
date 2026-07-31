import("stdfaust.lib");
declare name "Reese Machine";
declare description "Two or three detuned saws driven through two notch-and-clip stages, over a clean sine sub. The beating between the outer saws is the Reese; the notches carve it hollow and the clippers give it teeth.";
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

// Control labels must be unique across the whole patch, and none of them may be
// freq / gain / gate in any casing. The instrument host groups harvested voice
// controls by their bare label - that is how it collects the sixteen per-voice
// zones behind one parameter - so two controls sharing a label in different
// groups merge into a single parameter driving both, and a label matching a
// reserved voice control is dropped from the pool altogether. Hence the 1 / 2
// suffixes below rather than a bare "Drive" in each stage. They earn their keep
// twice over: macro links, MIDI Learn and automation targets list parameters
// flat, with no tab to disambiguate them.

// notchw designs its pole from wn = PI*(width/2)/SR, giving a pole radius of
// (1 - wn)/(1 + wn). That leaves the positive real axis once width passes
// 2*SR/PI - about 30 kHz at 48 kHz, 28 kHz at 44.1 - and the response stops
// being a notch. Width therefore has a real ceiling rather than a taste-based
// one; the slider stops short of it and this clamp keeps it honest at any
// sample rate. A width of exactly 0 would put the pole on the unit circle,
// which is why the sliders start at 1 Hz and not 0.
safeNotch(width, freq) = fi.notchw(min(width, ma.SR / 4.0), freq);

// ============================================================================
// Osc
// ============================================================================

// A pair of saws detuned symmetrically above and below the note, optionally
// with a third at the note itself. The slow beating between the outer two is
// the whole Reese sound, so Detune is the patch's primary control and small
// values are the useful ones - past roughly 30 cents it stops beating and
// starts sounding like a chord.
//
// Two outputs: the saw stack, and the sub. They are kept apart here because
// they take different routes downstream - see `voice`.
oscSection = vgroup("Osc", saws, sub)
with {
    detune = hslider("Detune [unit:ct] [idx:0]", 14, 0, 50, 0.1) : smoothed;
    // Cents to a frequency ratio. Symmetric in pitch, not in Hz, so the
    // beating rate tracks the note rather than drifting across the keyboard.
    ratio = pow(2.0, detune / 1200.0);
    subLevel = hslider("Sub [idx:14]", 0.5, 0.0, 1.0, 0.001) : smoothed;

    // 0 keeps the detuned pair alone, 1 adds the centre saw. Smoothed rather
    // than switched: the third saw is an audio signal being multiplied in, so
    // an instant 0 to 1 step would click. Over the 20 ms ramp the divisor
    // moves with it, which crossfades between the two voicings instead.
    third = nentry("Oscs [idx:15] [style:radio{'2':0;'3':1}]", 1, 0, 1, 1) : smoothed;

    // The detuned pair on its own is the Reese proper: nothing sits at the
    // played pitch, so the two saws beat against each other with nothing
    // anchoring them. The third saw plants the note underneath that movement,
    // which reads as more solid and less seasick. Dividing by the live count
    // keeps the level put across the switch.
    saws = (os.sawtooth(freq * ratio) +
            os.sawtooth(freq / ratio) +
            os.sawtooth(freq) * third) / (2.0 + third);

    // Not smoothed, unlike the oscillator count: os.osc accumulates phase, so
    // changing its frequency changes the increment while the phase stays
    // continuous. The octave jump is therefore already click-free, and ramping
    // it would turn a switch into a 20 ms portamento.
    subUnison = nentry("Sub Pitch [idx:16] [style:radio{'-1 Oct':0;'Unison':1}]", 0, 0, 1, 1);

    // A sine, not a saw: the sub is there for weight, and any harmonics it
    // carried would collide with the saw stack rather than reinforce it. That
    // matters more at Unison, where it sits in the same octave as the saws
    // and only the fundamental is wanted.
    sub = os.osc(freq * (0.5 + 0.5 * subUnison)) * subLevel;
};

// ============================================================================
// Stages
// ============================================================================

// Notch, then soft clip - twice. Ordering matters both times: the notch feeds
// the clipper, so it decides which part of the spectrum gets driven hardest,
// and the second notch then carves the harmonics the first clipper generated.
// Two stages in series is what separates this from a detuned saw patch.
//
// Width is the notch's approximate -3 dB width in Hz, not a Q, so a setting
// stays the same number of Hz wide as the notch sweeps up and is therefore
// proportionally narrower up there. That is the right behaviour here: these
// notches are formants placed against a bass, so they want to hold their
// absolute size rather than track the note.
//
// Bias offsets the signal before the nonlinearity, which adds even harmonics
// and asymmetry. cubicnl_nodc rather than cubicnl: the offset would otherwise
// leave a DC component behind, and two stages of it would compound.
stage1(x) = vgroup("Stage 1", x : safeNotch(width, nfreq) : ef.cubicnl_nodc(drive, bias))
with {
    nfreq = hslider("Notch 1 [unit:Hz] [scale:log] [scaleAnchor:500] [idx:1]",
                    300, 20, 18000, 1) : smoothed;
    width = hslider("Width 1 [unit:Hz] [scale:log] [scaleAnchor:300] [idx:2]",
                    200, 1, 12000, 1) : smoothed;
    drive = hslider("Drive 1 [idx:3]", 0.4, 0, 1, 0.001) : smoothed;
    bias = hslider("Bias 1 [idx:4]", 0.0, 0.0, 0.5, 0.001) : smoothed;
};

stage2(x) = vgroup("Stage 2", x : safeNotch(width, nfreq) : ef.cubicnl_nodc(drive, bias))
with {
    nfreq = hslider("Notch 2 [unit:Hz] [scale:log] [scaleAnchor:500] [idx:5]",
                    900, 20, 18000, 1) : smoothed;
    width = hslider("Width 2 [unit:Hz] [scale:log] [scaleAnchor:300] [idx:6]",
                    400, 1, 12000, 1) : smoothed;
    drive = hslider("Drive 2 [idx:7]", 0.3, 0, 1, 0.001) : smoothed;
    bias = hslider("Bias 2 [idx:8]", 0.0, 0.0, 0.5, 0.001) : smoothed;
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
