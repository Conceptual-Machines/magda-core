declare name "MagdaFreqShift";
declare description "Stereo single-sideband frequency shifter — shifts the spectrum by a fixed Hz offset.";

import("stdfaust.lib");

// ============================================================================
// User controls — pinned to [idx:N] for stable host-slot ordering.
// ============================================================================
shiftHz = hslider("Shift [unit:Hz] [idx:0]", 0.0, -1000.0, 1000.0, 0.1)
        : si.smooth(ba.tau2pole(0.05));

feedback = hslider("Feedback [idx:1]", 0.0, -0.9, 0.9, 0.01)
         : si.smooth(ba.tau2pole(0.02));

mix = hslider("Mix [idx:2]", 0.5, 0.0, 1.0, 0.001)
    : si.smooth(ba.tau2pole(0.02));

spread = hslider("Spread [idx:3]", 0.0, 0.0, 1.0, 0.01)
       : si.smooth(ba.tau2pole(0.05));

// ============================================================================
// Hilbert transformer — two parallel allpass cascades whose outputs differ
// by ~90° across the audio band. Coefficients from the standard Olli
// Niemitalo set for ~80 dB image rejection in the upper sideband.
// ============================================================================
hilbertReal(x) = x : fi.allpassnn(2, (0.4799, 0.9784));
hilbertImag(x) = x : fi.allpassnn(2, (0.8404, 0.9959)) : mem;

// Bode-style single-sideband shifter for one channel at a given Hz offset.
// 1-in 1-out — the carrier phasor runs at the shift frequency and is
// complex-multiplied with the analytic signal.
shifter(hz, x) = hilbertReal(x) * cos(ph * 2.0 * ma.PI)
              - hilbertImag(x) * sin(ph * 2.0 * ma.PI)
with {
    ph = (hz / ma.SR) : (+ : ma.frac) ~ _;
};

// Recurrent shifter — output is fed back into input scaled by `feedback`.
// Classic Bode-shifter resonant artefacts at high feedback. 1-in 1-out:
// the `~` operator consumes one of the leading `+`'s inputs.
channelWithFb(hz) = ((+) : shifter(hz)) ~ *(feedback);

// Spread detunes R relative to L by up to ±25 Hz so the stereo image
// widens without breaking mono compatibility.
SPREAD_HALF_HZ = 25.0;
hzL = shiftHz - spread * SPREAD_HALF_HZ;
hzR = shiftHz + spread * SPREAD_HALF_HZ;

// ============================================================================
// Wet/dry mix.
// ============================================================================
process(L, R) = L * (1.0 - mix) + (L : channelWithFb(hzL)) * mix,
                R * (1.0 - mix) + (R : channelWithFb(hzR)) * mix;
