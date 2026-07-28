declare name "Beat Repeater";
declare description "Stable Host-Synchronized Real-Time Beat Repeater — Auto-adjusting micro-fades based on absolute loop window scale to protect high-speed modes from audio loss.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- Host Sync Controls ---
hostBPM = hslider("BPM [bpm:on]", 120.0, 10.0, 360.0, 1.0);

// --- 8 Interlocking Performance Timing Checkboxes ---
t1_128 = checkbox("1. Stutter Roll: 1/128 Bar");
t1_64  = checkbox("2. Stutter Roll: 1/64 Bar");
t1_32  = checkbox("3. Stutter Roll: 1/32 Bar");
t1_16  = checkbox("4. Stutter Roll: 1/16 Bar");
t1_8   = checkbox("5. Stutter Roll: 1/8 Bar");
t1_4   = checkbox("6. Stutter Roll: 1/4 Bar");
t1_2   = checkbox("7. Stutter Roll: 1/2 Bar");
t1_1   = checkbox("8. Stutter Roll: 1/1 Bar [Full Bar]");
outputMix = hslider("9. Dry/Wet Mix [%]", 100.0, 0.0, 100.0, 1.0) / 100.0;

process(left, right) = wetL, wetR
with {
    // --- 1. PERFORMANCE DETECTOR ---
    engineActive = (t1_128 + t1_64 + t1_32 + t1_16 + t1_8 + t1_4 + t1_2 + t1_1) > 0.0;

    // Master background sample clock
    masterClock = (+ (1) ~ _);

    // --- 2. 8-STAGE RHYTHMIC PRIORITY ENCODER ---
    mode = select2(t1_1 == 1.0,
            select2(t1_2 == 1.0,
                select2(t1_4 == 1.0,
                    select2(t1_8 == 1.0,
                        select2(t1_16 == 1.0,
                            select2(t1_32 == 1.0,
                                select2(t1_64 == 1.0, 0, 1),
                            2),
                        3),
                    4),
                5),
            6),
         7);

    // --- 3. ACCURATE BAR DIVISION MATHEMATICS ---
    samplesPerBar = (240.0 / max(1.0, hostBPM)) * ma.SR;

    barFraction = select2(mode == 7,
                    select2(mode == 6,
                        select2(mode == 5,
                            select2(mode == 4,
                                select2(mode == 3,
                                    select2(mode == 2,
                                        select2(mode == 1, 1.0/128.0, 1.0/64.0),
                                    1.0/32.0),
                                1.0/16.0),
                            1.0/8.0),
                        1.0/4.0),
                    1.0/2.0),
                 1.0);

    maxBuffer = 524288;
    loopWindow = max(16.0, samplesPerBar * barFraction);

    // --- 4. ENGINE CONTROLLER ---
    repeatEngine(sig) = select2(engineActive == 1.0, sig, stutterAudio)
    with {
        writePointer = int(masterClock % maxBuffer);

        // Capture the exact frame timestamp when the button turns ON
        captureAnchor = ba.latch(ba.impulsify(engineActive), masterClock);

        // The ramp now starts exactly at 0 and stays within tiny integer ranges,
        // completely preserving floating-point accuracy.
        localRamp = (masterClock - captureAnchor) % loopWindow;

        // Calculate clean reading placement relative to the locked capture point
        readPointer = int((captureAnchor + localRamp) % maxBuffer);

        // --- 5. ADAPTIVE HIGH-SPEED ANTI-CLICK FADE WINDOW ---
        maxFadeSamples = 0.0015 * ma.SR;
        fadeSamples = min(maxFadeSamples, loopWindow * 0.15);

        fadeIn  = max(0.0, min(1.0, localRamp / fadeSamples));
        fadeOut = max(0.0, min(1.0, (loopWindow - localRamp) / fadeSamples));
        windowEnvelope = fadeIn * fadeOut;

        // Clean memory extraction
        rawAudio = rwtable(maxBuffer, 0.0, writePointer, sig, readPointer);
        stutterAudio = rawAudio * windowEnvelope;
    };

    // --- 6. ROUTING ROUTINES ---
    spLeft  = repeatEngine(left);
    spRight = repeatEngine(right);

    wetL = (left  * (1.0 - outputMix)) + (spLeft  * outputMix);
    wetR = (right * (1.0 - outputMix)) + (spRight * outputMix);
};
