declare name "Beat Repeater";
declare description "Stable Host-Synchronized Real-Time Beat Repeater: Auto-adjusting micro-fades based on absolute loop window scale to protect high-speed modes from audio loss.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- Host Sync Controls ---
// The DSP is described as host-synchronised, so the host writes the live
// project tempo into this zone every block rather than the player dialling
// it in by hand. Hidden so it does not take a cell in the param grid.
hostBPM = hslider("BPM[idx:3][unit:bpm][role:projecttempo][hidden:1]", 120.0, 10.0, 360.0, 1.0);

// --- Roll ---
// Was eight interlocking checkboxes resolved by a priority encoder.
// Engage is momentary: the roll runs for as long as it is held, which is
// how the effect is played.
engage    = vgroup("Roll", button("Engage[idx:0][tooltip: Runs the stutter roll for as long as it is held.]"));
division  = vgroup("Roll", nentry("Division[idx:1][style:radio{'1/128':0;'1/64':1;'1/32':2;'1/16':3;'1/8':4;'1/4':5;'1/2':6;'1 Bar':7}]", 0, 0, 7, 1));
outputMix = vgroup("Mix", hslider("Dry/Wet[idx:2][unit:%]", 100.0, 0.0, 100.0, 1.0)) / 100.0;

process(left, right) = wetL, wetR
with {
    // --- 1. PERFORMANCE DETECTOR ---
    engineActive = engage > 0.0;

    // Master background sample clock
    masterClock = (+ (1) ~ _);

    // --- 2. RHYTHMIC DIVISION ---
    mode = division;

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
