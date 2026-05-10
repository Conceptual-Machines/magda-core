declare name "MagdaDelay";
declare description "Stereo digital delay with tempo sync, feedback tone, and ping-pong cross-feedback.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Pin Time / Division / Sync to the first three pool slots so they
// render as adjacent cells in the inspector grid (Faust's default
// alphabetic harvest order would otherwise scatter them).

// Free-time delay (used when sync is off). Gated: greyed when Sync (slot 2)
// is ON, because the free-time value has no effect while sync is active.
time      = hslider("Time [unit:ms] [idx:0] [gate:!2]", 250, 1, 2000, 1)
            : si.smooth(ba.tau2pole(0.05));   // 50 ms parameter smoothing — keeps
                                              // automating Time from pitch-bending
                                              // the buffer pointer.

// Note division when synced. Encoded as the duration of one note relative
// to a quarter-note: 1/16 = 0.25, 1/8 = 0.5, 1/4 = 1, 1/2 = 2, 1/1 = 4.
// Dotted variants are 1.5×, triplet variants are (2/3)×.
// Gated: greyed when Sync (slot 2) is OFF, because the division value is
// unused in free-time mode.
division  = nentry("Division [idx:1] [gate:2] [style:menu{
                    '1/32':0.125;
                    '1/16T':0.16667;
                    '1/16':0.25;
                    '1/16.':0.375;
                    '1/8T':0.33333;
                    '1/8':0.5;
                    '1/8.':0.75;
                    '1/4T':0.66667;
                    '1/4':1.0;
                    '1/4.':1.5;
                    '1/2T':1.33333;
                    '1/2':2.0;
                    '1/2.':3.0;
                    '1/1':4.0
                  }]", 1.0, 0.125, 4.0, 0.001);

// Sync toggle: 0 = free time (ms), 1 = follow project BPM × division.
sync      = checkbox("Sync [idx:2]");

feedback  = hslider("Feedback [idx:3]", 0.45, 0.0, 0.95, 0.001);
mix       = hslider("Mix [idx:4]",      0.35, 0.0, 1.0,  0.001);

// Tilt EQ in the feedback path. Negative tilts dark (lowpass dominant);
// positive tilts bright (highpass dominant); 0 is flat.
tilt      = hslider("Tone [idx:5]",     0.0, -1.0, 1.0,  0.001);

// 0% = parallel stereo (each channel feeds back into itself).
// 100% = full ping-pong (each channel's tail feeds the other).
cross     = hslider("Cross [idx:6]",    0.0,  0.0, 1.0,  0.001);

// Hidden host-driven BPM. The MAGDA host writes the live project tempo
// into this slot's zone every audio block via the
// FaustControlRole::ProjectTempo plumbing — see FaustParamSlot.
//
// `[idx:63]` pins it to the last pool slot so the placeholder cell the
// param grid renders for hidden slots ends up on a far page, out of
// the way of the user controls.
bpm       = nentry("BPM [role:projectTempo] [hidden:1] [idx:63]",
                   120.0, 20.0, 999.0, 0.001);

// ============================================================================
// DSP
// ============================================================================

MAX_DELAY_SAMPLES = 192000;     // ~4 s at 48 kHz, headroom for higher rates.

// Delay time in samples for the synced branch.  One quarter-note = 60/bpm
// seconds, so `division × 60 / bpm × SR` samples for an arbitrary
// quarter-note multiple.
syncedSamples  = division * 60.0 / max(bpm, 1.0) * ma.SR;

// Free-time branch, in samples.
freeSamples    = time * ma.SR / 1000.0;

// Pick the active branch.  `sync` is 0 or 1. Smooth the blended result so
// flipping Sync (or stepping Division, or BPM changes) doesn't slam the
// read pointer to a new offset — that produced an audible click and a
// brief feedback runaway as the buffer pointer jumped past in-flight
// echoes. ~50 ms one-pole keeps the transition inaudible while still
// settling fast enough to feel responsive.
delaySamples   = ((1.0 - sync) * freeSamples + sync * syncedSamples)
                 : si.smooth(ba.tau2pole(0.05));

// Tilt EQ: blend a 1 kHz lowpass and a 1 kHz highpass against the dry
// signal.  At tilt = 0 we pass `x` through unchanged.
tone(x) = x * (1.0 - abs(tilt))
        + (x : fi.lowpass (2, 1000.0)) * max(0.0, -tilt)
        + (x : fi.highpass(2, 1000.0)) * max(0.0,  tilt);

// One channel's delay line — fractional sample delay + tone shaping.
oneLine = de.fdelay(MAX_DELAY_SAMPLES, delaySamples) : tone;

// Cross-mix two recirculation taps.  cross = 0 keeps each tap on its own
// side; cross = 1 fully swaps them.
xmix(a, b) = a * (1.0 - cross) + b * cross,
             b * (1.0 - cross) + a * cross;

// Dry-input asymmetry. xmix alone does nothing for mono material because
// stereo-symmetric input (L = R) stays symmetric through every linear
// operation in the feedback loop — swapping equal taps yields equal taps.
// Routing more of the dry signal toward L as cross→1 (R toward silence)
// breaks that symmetry: the first echo lands on L, bounces to R via the
// swapped feedback, back to L, and so on — classic ping-pong. Stereo
// material gets gently mono'd at the wet input at high cross, which is
// the conventional ping-pong feel anyway. cross = 0 is a clean
// passthrough so parallel stereo behaviour is preserved.
inputCross(L, R) = L * (1.0 - cross) + (L + R) * 0.5 * cross,
                   R * (1.0 - cross);

// Stereo delay with cross-feedback. See the delay PR description for the
// 4-in/2-out body + 2-in/2-out feedback path that `~` glues together.
stereoDelay = inputCross
              : ((ro.interleave(2, 2) : (+, +) : (oneLine, oneLine))
                 ~ (xmix : par(i, 2, *(feedback))));

// Wet/dry mix.  Split the dry pair, run the wet pair through stereoDelay,
// scale, and sum.  The :> n→2 merge sums (in0+in2, in1+in3), giving
// (dryL*(1-mix)+wetL*mix, dryR*(1-mix)+wetR*mix).
process = _, _
        <: (_, _), stereoDelay
        :  par(i, 2, *(1.0 - mix)), par(i, 2, *(mix))
        :> _, _;
