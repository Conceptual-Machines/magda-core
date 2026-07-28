declare name "MagdaRingMod";
declare description "Stereo ring modulator — multiply by an internal oscillator or a sidechain carrier.";

import("stdfaust.lib");

// ============================================================================
// User controls — pinned to [idx:N] for stable host-slot ordering.
// ============================================================================
sync = checkbox("Sync [idx:0]");

freq_hz = hslider("Frequency [unit:Hz] [scale:log] [scaleAnchor:200] [idx:1] [gate:!0]",
                  100.0, 1.0, 5000.0, 0.01)
        : si.smooth(ba.tau2pole(0.02));

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

shape = nentry("Shape [idx:3] [style:menu{'Sine':0;'Triangle':1;'Square':2}]",
               0, 0, 2, 1);

mix = hslider("Mix [idx:4]", 0.5, 0.0, 1.0, 0.001)
    : si.smooth(ba.tau2pole(0.02));

width = hslider("Width [idx:5]", 0.5, 0.0, 1.0, 0.01)
      : si.smooth(ba.tau2pole(0.05));

// Source selects whether the carrier is the internal oscillator or the
// host-routed sidechain bus (channel 3 of the input). Switching to
// Sidechain with nothing wired feeds silence in → output collapses to
// dry * (1 - mix), which is a clear "you forgot to route something" hint.
source = nentry("Source [idx:6] [style:menu{'Oscillator':0;'Sidechain':1}]",
                0, 0, 1, 1);

bpm = nentry("BPM [role:projectTempo] [hidden:1] [idx:63]",
             120.0, 20.0, 999.0, 0.001);

// ============================================================================
// Carrier — phase-shifted per channel for stereo width.
// ============================================================================
syncedHz = bpm / (60.0 * max(division, 0.001));
carrierHz = ((1.0 - sync) * freq_hz + sync * syncedHz)
          : si.smooth(ba.tau2pole(0.05));

phaseAt(off) = os.lf_sawpos(carrierHz) + off : ma.frac;

triangleFromPhase(p) = 4.0 * abs(p - 0.5) - 1.0;
squareFromPhase(p)   = select2(p < 0.5, -1.0, 1.0);

oscCarrierAt(off) = ba.selectn(3, int(shape),
                               sin(phaseAt(off) * 2.0 * ma.PI),
                               triangleFromPhase(phaseAt(off)),
                               squareFromPhase(phaseAt(off)));

carrierL(sc) = (1.0 - source) * oscCarrierAt(0.0) + source * sc;
carrierR(sc) = (1.0 - source) * oscCarrierAt(0.5 * width) + source * sc;

// ============================================================================
// Wet/dry mix; SC is the mono sidechain bus (channel 3).
// ============================================================================
process(L, R, SC) = L * (1.0 - mix) + L * carrierL(SC) * mix,
                    R * (1.0 - mix) + R * carrierR(SC) * mix;
