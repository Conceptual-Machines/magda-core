declare name "Reverser";
declare description "Real-Time Musical Phrase Reverser — Captures live audio blocks into a rolling buffer and reads them backwards across 4 button-activated sync grids. Instructions: Click any of the 4 checkboxes to change the reverse window size. Use the Mix fader to blend your dry audio back in.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- 4 Interlocking Timing Checkbox Buttons ---
t1_8Bar   = checkbox("1. Engage 1/8-Bar Reverse [tooltip: Triggers rapid backward glitch repeats.]");
t1_4Bar   = checkbox("2. Engage 1/4-Bar Reverse [tooltip: Triggers a fast rhythmic reverse skip.]");
t1_2Bar   = checkbox("3. Engage 1/2-Bar Reverse [tooltip: Triggers a classic pumping trap reverse phrase.]");
t1Bar     = checkbox("4. Engage 1-Bar Reverse [tooltip: Triggers standard 1-bar phrase reversal.]");

// Request Profile: Runs 100% wet by default
outputMix = hslider("5. Dry/Wet Mix [%] [tooltip: Blends your original clean signal back into the 100% wet reversed texture.]", 100.0, 0.0, 100.0, 1.0) / 100.0;

process(left, right) = wetL, wetR
with {
    engineActive = (t1_8Bar + t1_4Bar + t1_2Bar + t1Bar) > 0.0;

    mode = ba.if(t1Bar == 1.0, 3, ba.if(t1_2Bar == 1.0, 2, ba.if(t1_4Bar == 1.0, 1, 0)));

    loopWindow = ba.selectn(4, mode, 12000, 24000, 48000, 96000);

    maxBuffer = 400000;
    globalClock  = (+ (1) ~ _);
    writePointer = globalClock % maxBuffer;

    reverseEngine(sig) = select2(engineActive == 0.0, reversedAudio, sig)
    with {
        tapeMemory = sig : delay(maxBuffer, writePointer);
        intRamp = globalClock % loopWindow;

        readOffset = loopWindow - 1 - intRamp;
        readPointer = max(0, min(maxBuffer - 1, int(writePointer - 500 - readOffset)));

        reversedAudio = sig @ readPointer;
    };

    spLeft  = reverseEngine(left);
    spRight = reverseEngine(right);

    wetL = (left  * (1.0 - outputMix)) + (spLeft  * outputMix);
    wetR = (right * (1.0 - outputMix)) + (spRight * outputMix);
};

