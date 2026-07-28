declare name "8 Stage Morphable Wavefolder";
declare description "Ultra-Aggressive 8-Frame Chaotic Morphing Wavetable Wavefolder Stereo Audio Effect under Faust 2.70.3. Instructions: Use the Morph controls to sweep through 8 distinct harmonic frames, and drive the Wavefolder block to generate biting distortion textures.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";;

import("stdfaust.lib");

// ========================================================================
// THREAD-SAFE ALGEBRAIC SEED GENERATORS (Zero Memory Footprint)
// ========================================================================
chaosGen(offset) = finalValue
with {
    lcg = ( + (1103515245 + offset) ~ * (12345) ) : % (2147483648);
    finalValue = (float(lcg) / 1073741824.0) - 1.0;
};

// ========================================================================
// CORE GENERATION ENGINES
// ========================================================================
morphingFxCore(manualMorph, envTracking, randTrigger, sig) = finalMorphedStream
with {
    envFollow = abs(sig) : max ~ (*(exp(-1.0 / (0.05 * ma.SR)))); 
    totalMorph = max(0.0, min(7.0, manualMorph + (envFollow * envTracking)));

    h1_1 = chaosGen(7)   : ba.latch(randTrigger); h1_2 = chaosGen(13)  : ba.latch(randTrigger); h1_5 = chaosGen(61)  : ba.latch(randTrigger);
    h2_1 = chaosGen(101) : ba.latch(randTrigger); h2_5 = chaosGen(199) : ba.latch(randTrigger);
    h3_1 = chaosGen(223) : ba.latch(randTrigger); h3_2 = chaosGen(241) : ba.latch(randTrigger);
    h4_1 = chaosGen(311) : ba.latch(randTrigger); h4_5 = chaosGen(389) : ba.latch(randTrigger);
    h5_1 = chaosGen(419) : ba.latch(randTrigger); h5_5 = chaosGen(467) : ba.latch(randTrigger);
    h6_1 = chaosGen(503) : ba.latch(randTrigger); h6_2 = chaosGen(557) : ba.latch(randTrigger);
    h7_1 = chaosGen(601) : ba.latch(randTrigger); h7_5 = chaosGen(659) : ba.latch(randTrigger);
    h8_1 = chaosGen(701) : ba.latch(randTrigger); h8_2 = chaosGen(769) : ba.latch(randTrigger);

    wave1  = max(-1.0, min(1.0, (abs((sig * (1.5 + abs(h1_1 * 2.5)))) * 1.5) - 0.5)) * (1.0 + h1_2 * 0.2);
    drive2 = sig * 6.0; rect2 = select2(h2_5 > 0.0, drive2, abs(drive2) * 2.5 - 1.0); wave2 = max(-1.0, min(1.0, rect2 - (rect2^3 / 3.0))) * 0.8;
    wave3  = sin(sig * (h3_1 * 12.0) * ma.PI) * (1.5 + h3_2 * 2.0);
    quantLevels = 2.0 + abs(h4_1 * 6.0); wave4 = max(-1.0, min(1.0, int(sig * quantLevels * 3.0) / quantLevels));
    drive5 = sig * (12.0 + abs(h5_1 * 20.0)); wrap5 = sin(drive5 * ma.PI * 0.5); wave5 = max(-1.0, min(1.0, ba.if(wrap5 > 0.0, wrap5, wrap5 * -3.5 * h5_5)));
    drive6 = sig * (5.0 + abs(h6_1 * 5.0)); poly6 = (4.0 * drive6^3 - 3.0 * drive6) * (1.0 + h6_2); wave6 = max(-1.0, min(1.0, fmod(poly6, 0.5) * 2.0));
    drive7 = sig * (25.0 + abs(h7_1 * 30.0)); sat7 = ma.tanh(drive7); wave7 = ba.if(sat7 > 0.0, max(0.2, sat7 * 1.5), min(-0.2, sat7 * 1.5 * (1.0 - abs(h7_5))));
    wave8  = cos(sig * ma.PI * (15.0 + abs(h8_1 * 30.0))) * sin(sig * (h8_2 * 5.0));

    blend1 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 0.0))); blend2 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 1.0)));
    blend3 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 2.0))); blend4 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 3.0)));
    blend5 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 4.0))); blend6 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 5.0)));
    blend7 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 6.0))); blend8 = max(0.0, min(1.0, 1.0 - abs(totalMorph - 7.0)));

    finalMorphedStream = (wave1 * blend1) + (wave2 * blend2) + (wave3 * blend3) + (wave4 * blend4) +
                         (wave5 * blend5) + (wave6 * blend6) + (wave7 * blend7) + (wave8 * blend8);
};

wavefolderSection(foldDrive, foldSymmetry, sig) = foldedSignal
with {
    linearDrive  = ba.db2linear(foldDrive);
    biasedInput  = (sig * linearDrive) + foldSymmetry;
    foldedSignal = sin(biasedInput * ma.PI * 0.5);
};

// ========================================================================
// UNIFIED MASTER GRAPHICAL USER INTERFACE BLOCK
// ========================================================================
// Wrapping the entire parameter layout tree in one parent vertical group 
// explicitly forces the host DAW to render them sequentially from 1 to 9.
uiLayout(leftIn, rightIn) = finalL, finalR
with {
    masterMix    = hslider("1. Master Mix [%]", 25.0, 0.0, 100.0, 1.0) / 100.0;
    masterGain   = hslider("2. Master Volume [dB]", 5.0, -12.0, 12.0, 0.1) : ba.db2linear;
    
    randTrigger  = button("3. Re-Roll Chaos Seeds");
    manualMorph  = hslider("4. Base Frame Morph [0-7]", 0.0, 0.0, 7.0, 0.01);
    envTracking  = hslider("5. Dynamic Input Morph Depth", 2.0, 0.0, 3.0, 0.01);
    
    foldDrive    = hslider("6. Wavefolder Drive [dB]", 0.0, 0.0, 36.0, 0.1);
    foldSymmetry = hslider("7. Folding DC Symmetry", 0.0, -0.5, 0.5, 0.01);
    
    cutoff       = hslider("8. Resonant Cutoff [Hz]", 18000, 50, 18000, 1);
    res          = hslider("9. Resonance Value", 0.02, 0.0, 0.95, 0.01);
    safeRes      = max(0.01, res);

    // Operational signal routing paths
    wetL = leftIn  : morphingFxCore(manualMorph, envTracking, randTrigger) 
                   : wavefolderSection(foldDrive, foldSymmetry) 
                   : fi.resonlp(cutoff, safeRes, 1);
                   
    wetR = rightIn : morphingFxCore(manualMorph, envTracking, randTrigger) 
                   : wavefolderSection(foldDrive, foldSymmetry) 
                   : fi.resonlp(cutoff, safeRes, 1);

    mixL = (leftIn  * (1.0 - masterMix)) + (wetL * masterMix);
    mixR = (rightIn * (1.0 - masterMix)) + (wetR * masterMix);

    clip(x) = ba.if(x > 1.0, 1.0, ba.if(x < -1.0, -1.0, x));
    finalL  = clip(mixL * masterGain);
    finalR  = clip(mixR * masterGain);
};

// ========================================================================
// MAIN ROUTING PROCESS
// ========================================================================
process = vgroup("8 Stage Morphable Wavefolder", uiLayout);

