import("stdfaust.lib");
declare name "Reese Machine";
declare description "Two or three detuned saws driven through two notch-and-clip chains, over a clean sine sub. The beating between the outer saws is the Reese; the notches carve it hollow and the clippers give it teeth.";
declare author "MAGDA";
declare license "GPL-3.0";
declare version "1.0";

// Reserved per-voice controls: the polyphonic allocator drives these from the
// MIDI note and gate, so they are never exposed as pool parameters.
//
// `gain` is deliberately absent. Faust's allocator writes MIDI velocity into a
// control by that name if the patch declares one, and this patch does not want
// velocity anywhere near its amplitude - a Reese is a held, flat-topped sound,
// and velocity riding the level would also modulate how hard the clippers are
// driven from note to note. Leaving it undeclared is safe: dsp_voice collects
// gain paths into a list that simply comes back empty, and its velocity
// transform is default-constructed rather than left null.
freq = hslider("freq", 440, 20, 20000, 0.01);
gate = button("gate");

// Every continuous control is a per-voice zone the host writes from the message
// thread, so each one is smoothed before it reaches a filter coefficient or a
// gain. Without this a knob drag steps the notch frequency once per block.
smoothed = si.smooth(ba.tau2pole(0.02));

// ============================================================================
// Layout
// ============================================================================
//
// The device grid is eight cells wide and four rows deep per page, a page per
// vgroup. This patch declares no vgroup at all, so all thirteen controls share
// one page and nothing is hidden behind a tab - worth it for a patch whose
// whole point is hearing two chains interact.
//
// Row order is [idx] order and row breaks come from the widths summing to
// eight, so both are load-bearing here rather than decoration:
//
//     Notch 1  Width 1  Drive 1  Bias 1      idx 0-3,  2 cells each
//     Notch 2  Width 2  Drive 2  Bias 2      idx 4-7,  2 cells each
//     Detune  Oscs  Sub Pitch  Sub  Output   idx 8-12, 3 / 1 / 2 / 1 / 1
//
// Detune takes the widest cell of its row, being the patch's primary control.
// Sub Pitch needs two for its three segments; Oscs one for "2" and "3".
//
// Control labels must also be unique across the whole patch, and none may be
// freq / gain / gate in any casing. The instrument host groups harvested voice
// controls by their bare label - that is how it collects the sixteen per-voice
// zones behind one parameter - so two controls sharing a label merge into a
// single parameter driving both, and a label matching a reserved voice control
// is dropped from the pool entirely. Hence the 1 / 2 suffixes. They earn their
// keep twice over: macro links, MIDI Learn and automation targets list
// parameters flat, with no row or group to disambiguate them.

// notchw designs its pole from wn = PI*(width/2)/SR, giving a pole radius of
// (1 - wn)/(1 + wn). That leaves the positive real axis once width passes
// 2*SR/PI - about 30 kHz at 48 kHz, 28 kHz at 44.1 - and the response stops
// being a notch. Width therefore has a real ceiling rather than a taste-based
// one; the slider stops short of it and this clamp keeps it honest at any
// sample rate. A width of exactly 0 would put the pole on the unit circle,
// which is why the sliders start at 1 Hz and not 0.
safeNotch(width, freq) = fi.notchw(min(width, ma.SR / 4.0), freq);

// ============================================================================
// Chains (rows 1 and 2)
// ============================================================================
//
// Notch, then soft clip - twice. Ordering matters both times: the notch feeds
// the clipper, so it decides which part of the spectrum gets driven hardest,
// and the second notch then carves the harmonics the first clipper generated.
// Two chains in series is what separates this from a detuned saw patch.
//
// Width is the notch's approximate -3 dB width in Hz, not a Q, so a setting
// stays the same number of Hz wide as the notch sweeps and is therefore
// proportionally narrower up top. That is the right behaviour here: these
// notches are formants placed against a bass, so they want to hold their
// absolute size rather than track the note.
//
// Bias offsets the signal before the nonlinearity, which adds even harmonics
// and asymmetry. cubicnl_nodc rather than cubicnl: the offset would otherwise
// leave a DC component behind, and two chains of it would compound.
chain1(x) = x : safeNotch(width, nfreq) : ef.cubicnl_nodc(drive, bias)
with {
    nfreq = hslider("Notch 1 [unit:Hz] [scale:log] [scaleAnchor:500] [width:2] [idx:0]",
                    300, 20, 18000, 1) : smoothed;
    width = hslider("Width 1 [unit:Hz] [scale:log] [scaleAnchor:300] [width:2] [idx:1]",
                    200, 1, 12000, 1) : smoothed;
    drive = hslider("Drive 1 [width:2] [idx:2]", 0.4, 0, 1, 0.001) : smoothed;
    bias = hslider("Bias 1 [width:2] [idx:3]", 0.0, 0.0, 0.5, 0.001) : smoothed;
};

chain2(x) = x : safeNotch(width, nfreq) : ef.cubicnl_nodc(drive, bias)
with {
    nfreq = hslider("Notch 2 [unit:Hz] [scale:log] [scaleAnchor:500] [width:2] [idx:4]",
                    900, 20, 18000, 1) : smoothed;
    width = hslider("Width 2 [unit:Hz] [scale:log] [scaleAnchor:300] [width:2] [idx:5]",
                    400, 1, 12000, 1) : smoothed;
    drive = hslider("Drive 2 [width:2] [idx:6]", 0.3, 0, 1, 0.001) : smoothed;
    bias = hslider("Bias 2 [width:2] [idx:7]", 0.0, 0.0, 0.5, 0.001) : smoothed;
};

// ============================================================================
// Envelope
// ============================================================================

// Fixed, and not exposed. A Reese holds flat for as long as the note does, so
// the only envelope it wants is a click-free way in and out - there is no
// setting of these four a player would reach for mid-patch, and four knobs of
// grid space is a poor trade for that.
//
// asr rather than adsr for exactly that reason: at full sustain the decay stage
// runs from 1.0 to 1.0 and does nothing, so declaring it would only invite the
// question of what it is for. 5 ms in is fast enough to feel immediate and slow
// enough not to click on a low note.
env = en.asr(0.005, 1.0, 0.1, gate);

// ============================================================================
// Oscillators (row 3)
// ============================================================================

// Cents to a frequency ratio. Symmetric in pitch, not in Hz, so the beating
// rate tracks the note rather than drifting across the keyboard. Small values
// are the useful ones - past roughly 30 cents it stops beating and starts
// sounding like a chord.
detuneRatio = pow(2.0, detune / 1200.0)
with {
    detune = hslider("Detune [unit:ct] [width:3] [idx:8]", 14, 0, 50, 0.1) : smoothed;
};

// Two outputs: the saw stack, and the sub. They are kept apart here because
// they take different routes downstream - see `voice`.
oscSection = saws, sub
with {
    // 0 keeps the detuned pair alone, 1 adds the centre saw. Smoothed rather
    // than switched: the third saw is an audio signal being multiplied in, so
    // an instant 0 to 1 step would click. Over the 20 ms ramp the divisor moves
    // with it, which crossfades between the two voicings instead.
    third = nentry("Oscs [width:1] [idx:9] [style:radio{'2':0;'3':1}]", 1, 0, 1, 1) : smoothed;

    // Not smoothed, unlike the oscillator count: os.osc accumulates phase, so
    // changing its frequency changes the increment while the phase stays
    // continuous. The octave jump is therefore already click-free, and ramping
    // it would turn a switch into a 20 ms portamento.
    //
    // -2 is only useful in the upper half of the range: under an E1 it puts the
    // sub near 10 Hz, which is inaudible and spends headroom to be so.
    subOct = nentry("Sub Pitch [width:2] [idx:10] [style:radio{'-2':0;'-1':1;'0':2}]",
                    1, 0, 2, 1);
    // Octaves relative to the played note, so the labels are the offset itself.
    subRatio = pow(2.0, subOct - 2.0);

    subLevel = hslider("Sub [width:1] [idx:11]", 0.5, 0.0, 1.0, 0.001) : smoothed;

    // The detuned pair on its own is the Reese proper: nothing sits at the
    // played pitch, so the two saws beat against each other with nothing
    // anchoring them. The third saw plants the note underneath that movement,
    // which reads as more solid and less seasick. Dividing by the live count
    // keeps the level put across the switch.
    saws = (os.sawtooth(freq * detuneRatio) +
            os.sawtooth(freq / detuneRatio) +
            os.sawtooth(freq) * third) / (2.0 + third);

    // A sine, not a saw: the sub is there for weight, and any harmonics it
    // carried would collide with the saw stack rather than reinforce it. That
    // matters most at 0, where it sits in the same octave as the saws and only
    // the fundamental is wanted.
    sub = os.osc(freq * subRatio) * subLevel;
};

// Both clip chains add level, and Drive at the top of its range roughly doubles
// it, so the patch needs a trim to stay useful without re-gaining downstream.
// Default is -6 dB to leave headroom for sixteen voices.
outSection(x) = x * ba.db2linear(level)
with {
    level = hslider("Output [unit:dB] [idx:12]", -6, -24, 12, 0.1) : smoothed;
};

// ============================================================================
// Voice
// ============================================================================

// The saws take the notch/clip chains; the sub goes around them and rejoins at
// the output. Two clippers in series would flatten a sine into something closer
// to a square and hand its energy to harmonics an octave up, which is exactly
// the weight the sub is there to provide. Route it through them instead by
// changing this line to `: chain1 : chain2` after the merge.
//
// The envelope gates both oscillator paths ahead of the chains. At full sustain
// that means the clippers see a steady level for the whole note, so the amount
// of dirt is what the Drive knobs say it is and nothing else.
//
// Mono voice fanned to stereo: the beating is an amplitude effect within the
// single summed signal, so it survives the fan intact.
voice = oscSection : *(env), *(env) : (chain1 : chain2), _ :> outSection;
process = voice <: _, _;
