declare name "MagdaMod";
declare description "Modulation: tremolo / vibrato / auto-pan with shared LFO, free or tempo-synced.";

import("stdfaust.lib");

// ============================================================================
// User controls — pinned to [idx:N] for stable host-slot ordering.
// ============================================================================

mode      = nentry("Mode [idx:0] [style:menu{'Tremolo':0;'Vibrato':1;'Autopan':2}]",
                   0, 0, 2, 1);

// Sync toggle: 0 = free (Hz), 1 = follow project BPM × division.
sync      = checkbox("Sync [idx:1]");

// Free-rate (Hz). Greyed when Sync is on.
rate_hz   = hslider("Rate [unit:Hz] [scale:log] [scaleAnchor:4] [idx:2] [gate:!1]",
                    4.0, 0.05, 20.0, 0.01)
          : si.smooth(ba.tau2pole(0.05));

// Note division when synced. Encoded as the duration of one note relative to
// a quarter-note: 1/4 = 1, 1/8 = 0.5, etc. (same encoding the delay uses).
// Greyed when Sync is off.
division  = nentry("Division [idx:3] [gate:1] [style:menu{
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

depth     = hslider("Depth [idx:4]", 0.5, 0.0, 1.0, 0.01)
          : si.smooth(ba.tau2pole(0.02));

shape     = nentry("Shape [idx:5] [style:menu{'Sine':0;'Triangle':1;'Square':2;'S&H':3}]",
                   0, 0, 3, 1);

// Hidden host-driven BPM (see MagdaDelay for the same plumbing).
bpm       = nentry("BPM [role:projectTempo] [hidden:1] [idx:63]",
                   120.0, 20.0, 999.0, 0.001);

// ============================================================================
// LFO selection
// ============================================================================

// One quarter-note = 60/bpm seconds; division is in quarter-note units.
// So one cycle = division * 60/bpm seconds → freq = bpm / (60 * division).
syncedHz  = bpm / (60.0 * max(division, 0.001));

freqHz    = ((1.0 - sync) * rate_hz + sync * syncedHz)
          : si.smooth(ba.tau2pole(0.05));

// Sample-and-hold: latch white noise on each high half-period of a square
// wave at the LFO rate. Not a perfect impulse-edge S&H but read as a stepped
// random pattern in audio.
trig      = os.lf_squarewavepos(freqHz) > 0.5;
sAndH     = no.noise : ba.latch(trig);

sine      = os.osc(freqHz);
triangle  = os.lf_triangle(freqHz);
square    = os.lf_squarewave(freqHz);

// Bipolar LFO (-1..1). Selects one of the four shapes. All four branches run
// every sample; the cost is trivial for plain LFOs.
lfoBi     = ba.selectn(4, int(shape), sine, triangle, square, sAndH);
lfoUni    = 0.5 + 0.5 * lfoBi;

// ============================================================================
// Mode bodies (each takes stereo in → stereo out)
// ============================================================================

// Tremolo — amplitude modulation. Depth scales the floor: depth=0 → bypass,
// depth=1 → fully gated by the LFO.
tremGain  = (1.0 - depth) + depth * lfoUni;
trem(L, R) = L * tremGain, R * tremGain;

// Vibrato — short modulated delay line per channel. Centered so depth=0 is
// a clean passthrough (no delay applied).
MAX_VIB_MS = 5.0;
vibCenter  = MAX_VIB_MS * 0.5 * ma.SR / 1000.0;
vibMod     = lfoBi * depth * vibCenter * 0.9;
vibLine(x) = x : de.fdelay(512, vibCenter + vibMod);
vib(L, R)  = L * (1.0 - depth) + vibLine(L) * depth,
             R * (1.0 - depth) + vibLine(R) * depth;

// Autopan — equal-power pan. depth controls modulation amount; depth=0 keeps
// the signal centered (each channel multiplied by cos(π/4) ≈ 0.707).
panPos    = 0.5 + 0.5 * lfoBi * depth;
panL      = cos(panPos * ma.PI * 0.5);
panR      = sin(panPos * ma.PI * 0.5);
pan(L, R) = L * panL, R * panR;

// ============================================================================
// Dispatch
// ============================================================================

// Run all three modes in parallel then pick the active one. Same idiom the
// phaser uses for its 4-stage parallel selectn — CPU is small for LFO/utility
// effects.
process = _, _
        <: (trem, vib, pan)
        :  ro.interleave(2, 3)
        :  ba.selectn(3, int(mode)), ba.selectn(3, int(mode));
