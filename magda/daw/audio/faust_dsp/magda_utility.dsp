declare name "MagdaUtility";
declare description "Stereo utility: gain, pan, width, mono, low mono, per-channel polarity flip.";

import("stdfaust.lib");

// ============================================================================
// Controls
// Slots 0-3: knobs shown in param grid.
// Slots 4-7: toggle buttons shown in the custom panel, hidden from knob grid.
// ============================================================================

gainDb      = hslider("Gain [unit:dB] [idx:0]",    0.0, -60.0, 12.0, 0.1) : si.smooth(ba.tau2pole(0.02));
pan         = hslider("Pan [idx:1]",               0.0,  -1.0,  1.0, 0.01) : si.smooth(ba.tau2pole(0.02));
width       = hslider("Width [idx:2]",             1.0,   0.0,  2.0, 0.01) : si.smooth(ba.tau2pole(0.02));
lowMonoFreq = hslider("Low Mono Freq [unit:Hz] [scale:log] [scaleAnchor:120] [idx:3]", 120.0, 20.0, 500.0, 1.0) : si.smooth(ba.tau2pole(0.05));

mono    = checkbox("Mono [idx:4]");
lowMono = checkbox("Low Mono [idx:5]");
flipL   = checkbox("Flip L [idx:6]");
flipR   = checkbox("Flip R [idx:7]");

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);

// Equal-power pan: pan=-1 → full left, pan=0 → centre, pan=1 → full right.
panGainL = cos((pan + 1.0) * 0.25 * ma.PI);
panGainR = sin((pan + 1.0) * 0.25 * ma.PI);

process(inL, inR) = outL, outR
with {
    // Per-channel polarity flip.
    l0 = inL * (1.0 - 2.0 * flipL);
    r0 = inR * (1.0 - 2.0 * flipR);

    // Gain.
    l1 = l0 * db2lin(gainDb);
    r1 = r0 * db2lin(gainDb);

    // Stereo width via M/S encoding. Mono collapses width to 0.
    w    = width * (1.0 - mono);
    mid  = (l1 + r1) * 0.5;
    side = (l1 - r1) * 0.5;
    l2   = mid + side * w;
    r2   = mid - side * w;

    // Low mono: sum bass below lowMonoFreq to mono, leave highs stereo.
    // Only active when lowMono=1 and mono=0.
    lLow     = l2 : fi.lowpass(2, lowMonoFreq);
    rLow     = r2 : fi.lowpass(2, lowMonoFreq);
    lHigh    = l2 : fi.highpass(2, lowMonoFreq);
    rHigh    = r2 : fi.highpass(2, lowMonoFreq);
    bassMono = (lLow + rLow) * 0.5;
    doLM     = lowMono * (1.0 - mono);
    l3       = l2 * (1.0 - doLM) + (bassMono + lHigh) * doLM;
    r3       = r2 * (1.0 - doLM) + (bassMono + rHigh) * doLM;

    // Pan.
    outL = l3 * panGainL;
    outR = r3 * panGainR;
};
