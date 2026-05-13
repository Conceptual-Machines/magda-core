declare name "MagdaFlanger";
declare description "Stereo flanger — short modulated delay with feedback, sync- or free-rate.";

import("stdfaust.lib");

// ============================================================================
// User controls — pinned to [idx:N] for stable host-slot ordering.
// ============================================================================
sync = checkbox("Sync [idx:0]");

rate_hz = hslider("Rate [unit:Hz] [scale:log] [scaleAnchor:0.5] [idx:1] [gate:!0]",
                  0.5, 0.05, 10.0, 0.01)
        : si.smooth(ba.tau2pole(0.05));

division = nentry("Division [idx:2] [gate:0] [style:menu{
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

depth    = hslider("Depth [idx:3]", 0.5, 0.0, 1.0, 0.01)
         : si.smooth(ba.tau2pole(0.02));
feedback = hslider("Feedback [idx:4]", 0.0, -0.95, 0.95, 0.01)
         : si.smooth(ba.tau2pole(0.02));
mix      = hslider("Mix [idx:5]", 0.5, 0.0, 1.0, 0.001)
         : si.smooth(ba.tau2pole(0.02));
width    = hslider("Width [idx:6]", 0.5, 0.0, 1.0, 0.01)
         : si.smooth(ba.tau2pole(0.05));

bpm = nentry("BPM [role:projectTempo] [hidden:1] [idx:63]",
             120.0, 20.0, 999.0, 0.001);

// ============================================================================
// LFO selection
// ============================================================================
syncedHz = bpm / (60.0 * max(division, 0.001));
freqHz   = ((1.0 - sync) * rate_hz + sync * syncedHz)
         : si.smooth(ba.tau2pole(0.05));

lfoAt(phaseOffset) = sin((os.lf_sawpos(freqHz) + phaseOffset) * 2.0 * ma.PI);

// ============================================================================
// Flanger — single short modulated delay per channel with feedback.
// Centre ≈ 3 ms, swing ≈ ±2.5 ms — the tight range that gives the
// signature comb-filter sweep when fed back into itself.
// ============================================================================
CENTER_MS = 3.0;
SWING_MS  = 2.5;
samplesFor(ms_val) = ms_val * ma.SR / 1000.0;

flangerLine(side) =
    de.fdelay(512, samplesFor(CENTER_MS + lfoAt(side) * depth * SWING_MS));

flangerCh(side) = (+ : flangerLine(side)) ~ *(feedback);

// ============================================================================
// Wet/dry, stereo offset via `width`
// ============================================================================
process(L, R) = L * (1.0 - mix) + flangerCh(0.0)(L) * mix,
                R * (1.0 - mix) + flangerCh(0.5 * width)(R) * mix;
