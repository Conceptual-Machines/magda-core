declare name "Ultimate Glitcher";
declare description "True Parallel Polyrhythmic Glitch Matrix — Inspired by dblue Glitch 2 architecture. Features weighted stutter-freezing, a low-profile bitcrusher, parallel routing, and a protective master hard clipper.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- Global Engine Speed ---
masterRate = hslider("0. Global Master Speed [Hz]", 1.0, 0.1, 10.0, 0.01);

// --- Effect Lane Controls (Chance % and Intensity Parameters) ---
gChance     = hslider("1. Reverser: Probability [%]", 40.0, 0.0, 100.0, 1.0) / 100.0;
pitchRange  = hslider("1. Reverser: Max Pitch [Semitones]", 5.0, 1.0, 12.0, 1.0);
stereoWide  = checkbox("1. Reverser: Stereo Widener [On/Off]");

stChance    = hslider("2. Stutter-Freeze: Probability [%]", 35.0, 0.0, 100.0, 1.0) / 100.0;

bcChance    = hslider("3. Bitcrusher: Probability [%]", 30.0, 0.0, 100.0, 1.0) / 100.0;
bcBits      = hslider("3. Bitcrusher: Bit Depth [Bits]", 8.0, 2.0, 16.0, 0.5);

rmChance    = hslider("4. Ring Mod: Probability [%]", 20.0, 0.0, 100.0, 1.0) / 100.0; 
rmFreq      = hslider("4. Ring Mod: Carrier Freq [Hz]", 350.0, 50.0, 1200.0, 1.0);  

phChance    = hslider("5. Phaser: Probability [%]", 2.0, 0.0, 100.0, 1.0) / 100.0;
flChance    = hslider("6. Flanger: Probability [%]", 2.0, 0.0, 100.0, 1.0) / 100.0;

tsChance    = hslider("7. Master Tape Stop: Probability [%]", 35.0, 0.0, 100.0, 1.0) / 100.0;
tsTime      = hslider("7. Master Tape Stop: Duration [s]", 0.18, 0.05, 1.5, 0.01);

outputMix   = hslider("8. Master Mix [%]", 100.0, 0.0, 100.0, 1.0) / 100.0;

process(left, right) = limL, limR
with {
    globalClock = (+ (1) ~ _);

    // ========================================================================
    // 1. PARALLEL POLYRHYTHMIC TRIGGER CLOCKS
    // ========================================================================
    trigGlitch   = os.lf_triangle(masterRate * (1.0)) > 0.88;          
    trigStutter  = os.lf_triangle(masterRate * (15.0 / 16.0)) > 0.88; 
    trigCrusher  = os.lf_triangle(masterRate * (5.0 / 16.0)) > 0.88;  
    trigRingMod  = os.lf_triangle(masterRate * (13.0 / 16.0)) > 0.88;   
    trigPhaser   = os.lf_triangle(masterRate * (7.0 / 16.0)) > 0.88;    
    trigFlanger  = os.lf_triangle(masterRate * (9.0 / 16.0)) > 0.88;    
    trigTapeStop = os.lf_triangle(masterRate * (11.0 / 16.0)) > 0.88;   

    // Independent Dice Rolls
    runGlitch    = select2(((no.noise : ba.latch(trigGlitch)) + 1.0) / 2.0 < gChance, 0.0, 1.0);
    runStutter   = select2(((no.noise : ba.latch(trigStutter)) + 1.0) / 2.0 < stChance, 0.0, 1.0);
    runCrusher   = select2(((no.noise : ba.latch(trigCrusher)) + 1.0) / 2.0 < bcChance, 0.0, 1.0);
    runRingMod   = select2(((no.noise : ba.latch(trigRingMod)) + 1.0) / 2.0 < rmChance, 0.0, 1.0);
    runPhaser    = select2(((no.noise : ba.latch(trigPhaser)) + 1.0) / 2.0 < phChance, 0.0, 1.0);
    runFlanger   = select2(((no.noise : ba.latch(trigFlanger)) + 1.0) / 2.0 < flChance, 0.0, 1.0);
    tsDiceRoll   = select2(((no.noise : ba.latch(trigTapeStop)) + 1.0) / 2.0 < tsChance, 0.0, 1.0);

    // ========================================================================
    // 2. PARALLEL DSP EFFECT CHANNELS
    // ========================================================================
    
    // --- LANE 1: REVERSER + PITCH SHIFTER ---
    revEngine(isRightChan, sig) = finalAudio
    with {
        maxBuffer = 262144;
        channelOffset = select2(isRightChan * stereoWide, 0, 4800); 
        writePointer = int((globalClock + channelOffset) % maxBuffer);
        
        captureAnchor = ba.latch(trigGlitch, globalClock);
        localSamplesElapsed = globalClock - captureAnchor;

        shouldReverse = (no.noise : ba.latch(trigGlitch @ 50) > 0.0);
        semitoneShift = int((no.noise : ba.latch(trigGlitch @ 100)) * pitchRange); 
        pitchRatio = ba.semi2ratio(semitoneShift);

        playbackDelta = select2(shouldReverse, localSamplesElapsed * pitchRatio, (localSamplesElapsed * pitchRatio) * -1.0);
        readPointer = max(0, min(maxBuffer - 1, int((captureAnchor + playbackDelta + channelOffset) % maxBuffer)));

        fadeSamples = 0.005 * ma.SR; 
        windowEnvelope = max(0.0, min(1.0, localSamplesElapsed / fadeSamples));

        rawAudio = rwtable(maxBuffer, 0.0, writePointer, sig, readPointer);
        finalAudio = rawAudio * windowEnvelope * 0.9; 
    };

    // --- LANE 2: RECALIBRATED STUTTER-FREEZE ENGINE ---
    stutterEngine(sig) = finalAudio
    with {
        maxBuffer = 65536;
        writePointer = int(globalClock % maxBuffer);

        captureAnchor = ba.latch(trigStutter, globalClock);
        localSamplesElapsed = globalClock - captureAnchor;

        // Roll internal dice (0.0 to 1.0 range)
        rateSelector = ((no.noise : ba.latch(trigStutter @ 30)) + 1.0) / 2.0;

        // FIX: Removed 1/64 and 1/8 entirely.
        // Even 50/50 split across the random timeline between 1/32 and 1/16 divisions.
        samplesDiv = select2(rateSelector < 0.5, ma.SR / 16.0, ma.SR / 32.0);

        loopWindow = max(16.0, samplesDiv);
        localRamp = localSamplesElapsed % loopWindow;
        readPointer = max(0, min(maxBuffer - 1, int((captureAnchor + localRamp) % maxBuffer)));

        fadeSamples = min(48.0, loopWindow * 0.1); 
        fadeIn = max(0.0, min(1.0, localRamp / fadeSamples));
        fadeOut = max(0.0, min(1.0, (loopWindow - localRamp) / fadeSamples));
        windowEnvelope = fadeIn * fadeOut;

        rawAudio = rwtable(maxBuffer, 0.0, writePointer, sig, readPointer);
        finalAudio = rawAudio * windowEnvelope;
    };

    // --- LANE 3: DIGITAL BITCRUSHER ---
    crushEngine(sig) = finalAudio
    with {
        quantizationLevels = pow(2.0, max(2.0, min(16.0, bcBits)));
        finalAudio = int(sig * quantizationLevels) / quantizationLevels;
    };

    // --- LANE 4: RING MODULATOR ---
    ringModEngine(sig) = sig * os.osc(rmFreq) * 0.5;

    // --- LANE 5: PHASER ---
    phaserEngine(sig) = (sig + sweptDelay) * 0.5
    with {
        maxDel = 1024;
        sweepOsc = (os.lf_triangle(0.2) + 1.0) * 0.5; 
        delaySamples = int(10.0 + (sweepOsc * 120.0));
        sweptDelay = de.delay(maxDel, delaySamples, sig);
    };

    // --- LANE 6: FLANGER ---
    flangerEngine(sig) = (sig + (feedback * modulatedDelay)) * 0.5
    with {
        maxDel = 2048;
        feedback = 0.45; 
        sweepOsc = (os.lf_triangle(0.5) + 1.0) * 0.5; 
        delaySamples = int(15.0 + (sweepOsc * 160.0));
        modulatedDelay = de.delay(maxDel, delaySamples, sig);
    };

        // ========================================================================
    // 3. PARALLEL BUS MATRIX (Perfect Zero-Dampening Bypass)
    // ========================================================================
    mixBuses(isRight, sig) = select2(anyGlitchActive, sig, activeGlitches)
    with {
        v1 = revEngine(isRight, sig);
        v2 = stutterEngine(sig);
        v3 = crushEngine(sig);
        v4 = ringModEngine(sig);
        v5 = phaserEngine(sig);
        v6 = flangerEngine(sig);

        out1 = select2(runGlitch == 1.0,  0.0, v1);
        out2 = select2(runStutter == 1.0, 0.0, v2);
        out3 = select2(runCrusher == 1.0, 0.0, v3);
        out4 = select2(runRingMod == 1.0, 0.0, v4);
        out5 = select2(runPhaser == 1.0,  0.0, v5);
        out6 = select2(runFlanger == 1.0, 0.0, v6);

        activeCount = runGlitch + runStutter + runCrusher + runRingMod + runPhaser + runFlanger;
        anyGlitchActive = activeCount > 0.0;
        
        activeGlitches = (out1 + out2 + out3 + out4 + out5 + out6) / max(1.0, activeCount);
    };


    // ========================================================================
    // 4. MASTER FX LANE: ONE-SHOT TAPE STOP
    // ========================================================================
    masterTapeStop(sig) = finalAudio
    with {
        maxBuffer = 131072;
        writePointer = int(globalClock % maxBuffer);
        
        oneShotTrigger = ba.impulsify(trigTapeStop);
        
        stopSamples = tsTime * ma.SR;
        timeSinceTrigger = globalClock - ba.latch(oneShotTrigger, globalClock);
        inStopWindow = timeSinceTrigger < stopSamples;
        
        runTapeStop = (tsDiceRoll == 1.0) & inStopWindow;

        captureAnchor = ba.latch(oneShotTrigger, globalClock);
        localSamplesElapsed = globalClock - captureAnchor;

        progress = max(0.0, min(1.0, float(localSamplesElapsed) / stopSamples));
        tapeSpeed = (1.0 - progress) * (1.0 - progress); 

        readPointer = max(0, min(maxBuffer - 1, int((captureAnchor + ((tapeSpeed : (+ ~ _)) * -1.0)) % maxBuffer)));

        rawAudio = rwtable(maxBuffer, 0.0, writePointer, sig, readPointer);
        gateEnvelope = select2(progress >= 1.0, 1.0, 0.0);
        
        finalAudio = select2(runTapeStop == 1.0, sig, rawAudio * gateEnvelope);
    };

    // ========================================================================
    // 5. MASTER HARD CLIPPER BLOCK (Strict 0.0 dBFS Ceiling)
    // ========================================================================
    glitchedL = mixBuses(0, left)  : masterTapeStop;
    glitchedR = mixBuses(1, right) : masterTapeStop;

    wetL = (left  * (1.0 - outputMix)) + (glitchedL * outputMix);
    wetR = (right * (1.0 - outputMix)) + (glitchedR * outputMix);

    // Hard clip instantly between -1.0 and 1.0
    clip(x) = ba.if(x > 1.0, 1.0, ba.if(x < -1.0, -1.0, x));

    limL = clip(wetL);
    limR = clip(wetR);
};
