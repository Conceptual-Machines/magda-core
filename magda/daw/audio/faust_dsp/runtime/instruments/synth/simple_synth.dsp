import("stdfaust.lib");
declare name "Simple Synth";
declare description "Detuned saw pair through a resonant lowpass. Replace this with your own DSP.";
declare author "MAGDA";
declare license "GPL-3.0";
declare version "1.0";

// The starting point a fresh Faust Instrument loads with. It lives here as well
// as in the plugin's built-in default so it can be loaded back: without a copy
// in the bundled library, loading any other patch was a one-way door and the
// only route back was creating a new device.
//
// Keep the two in step. The built-in copy has to stay compiled in for the case
// this file cannot be read - a dev build with nothing staged, or a broken
// install - so the device always comes up with something that makes sound.

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// Osc tab: detuned sub-saw mixed under the main saw.
oscSection = vgroup("Osc", os.sawtooth(freq) + sub * os.sawtooth(freq * 0.5))
with {
    sub = hslider("sub [idx:4]", 0.0, 0.0, 1.0, 0.01);
};

// Filter tab: resonant lowpass.
filterSection(x) = vgroup("Filter", x : fi.resonlp(cutoff, q, 1))
with {
    cutoff = hslider("cutoff [unit:Hz] [scale:log] [idx:0]", 3000, 50, 18000, 1);
    res    = hslider("resonance [idx:1]", 0.3, 0, 1, 0.01);

    // A resonance and a Q are not the same number, and resonlp's second
    // argument is a Q. It computes 1/Q, so a control handed to it unchanged
    // divides by zero at the bottom of its travel: the filter's feedback
    // coefficient comes out NaN, the voice outputs NaN from its first sample,
    // and it never recovers because that NaN is now the filter's own state
    // (#2237).
    //
    // 0.707 is Butterworth, the flattest a two-pole lowpass gets, so the bottom
    // of the control is no emphasis rather than no filter. Squared rather than
    // linear so the resonant half of the range is spread over half the control
    // instead of arriving in the last few percent of it.
    //
    // The old mapping was the control itself, capped at 0.95, so every position
    // on it was below Butterworth: it ran from heavily damped to slightly less
    // damped and never resonated anywhere. The cap went with the singularity it
    // was avoiding.
    q = 0.707 + res * res * 27.3;
};

// Env tab: ADSR amplitude envelope.
envSection = vgroup("Env", en.adsr(att, 0.2, 0.7, rel, gate))
with {
    att = hslider("attack [unit:s] [idx:2]", 0.005, 0.001, 2, 0.001);
    rel = hslider("release [unit:s] [idx:3]", 0.4, 0.001, 4, 0.001);
};

voice = oscSection * envSection * gain : filterSection;
process = voice <: _, _;
