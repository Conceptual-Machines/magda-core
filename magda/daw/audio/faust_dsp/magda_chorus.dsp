declare name "MagdaChorus";
declare description "Stereo chorus — 1 to 3 modulated voices per side, sync- or free-rate.";

import("stdfaust.lib");

// ============================================================================
// User controls — pinned to [idx:N] for stable host-slot ordering.
// ============================================================================
voices = nentry("Voices [idx:0] [style:menu{'1':0;'2':1;'3':2}]", 1, 0, 2, 1);
voices_n = int(voices) + 1;

sync = checkbox("Sync [idx:1]");

rate_hz = hslider("Rate [unit:Hz] [scale:log] [scaleAnchor:0.5] [idx:2] [gate:!1]",
                  0.5, 0.05, 10.0, 0.01)
        : si.smooth(ba.tau2pole(0.05));

division = nentry("Division [idx:3] [gate:1] [style:menu{
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

depth    = hslider("Depth [idx:4]", 0.5, 0.0, 1.0, 0.01)
         : si.smooth(ba.tau2pole(0.02));
feedback = hslider("Feedback [idx:5]", 0.0, -0.9, 0.9, 0.01)
         : si.smooth(ba.tau2pole(0.02));
mix      = hslider("Mix [idx:6]", 0.5, 0.0, 1.0, 0.001)
         : si.smooth(ba.tau2pole(0.02));
width    = hslider("Width [idx:7]", 0.5, 0.0, 1.0, 0.01)
         : si.smooth(ba.tau2pole(0.05));

bpm = nentry("BPM [role:projectTempo] [hidden:1] [idx:63]",
             120.0, 20.0, 999.0, 0.001);

// ============================================================================
// LFO selection
// ============================================================================
syncedHz = bpm / (60.0 * max(division, 0.001));
freqHz   = ((1.0 - sync) * rate_hz + sync * syncedHz)
         : si.smooth(ba.tau2pole(0.05));

// Phase-shifted sine. os.lf_sawpos returns 0..1; we offset then wrap into a sine.
lfoAt(phaseOffset) = sin((os.lf_sawpos(freqHz) + phaseOffset) * 2.0 * ma.PI);

// ============================================================================
// Voice topology — three modulated delay lines per channel; only the first
// `voices_n` are heard. Width spreads voice phases and stereo offset.
// ============================================================================
CENTER_MS = 18.0;
SWING_MS  = 12.0;
samplesFor(ms_val) = ms_val * ma.SR / 1000.0;

voiceDelay(i, side) = de.fdelay(
    2048,
    samplesFor(CENTER_MS + lfoAt(i / 3.0 * width + side) * depth * SWING_MS));

voiceMask(i) = ba.if(voices_n > i, 1.0, 0.0);

voiceSumOf(side) = _ <: voiceDelay(0, side), voiceDelay(1, side), voiceDelay(2, side)
                      : *(voiceMask(0)), *(voiceMask(1)), *(voiceMask(2))
                      :> _ / float(max(1, voices_n));

// One channel with a feedback loop around the voice sum. Same `~` pattern
// the delay uses for its recirculating echoes.
chorusCh(side) = (+ : voiceSumOf(side)) ~ *(feedback);

// ============================================================================
// Wet/dry, stereo offset via `width`
// ============================================================================
process(L, R) = L * (1.0 - mix) + chorusCh(0.0)(L) * mix,
                R * (1.0 - mix) + chorusCh(0.5 * width)(R) * mix;
