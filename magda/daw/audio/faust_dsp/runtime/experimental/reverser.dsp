declare name "Reverser";
declare description "Real-Time Musical Phrase Reverser — Captures live audio blocks into a rolling buffer and reads them backwards across 4 button-activated sync grids. Instructions: Click any of the 4 checkboxes to change the reverse window size. Use the Mix fader to blend your dry audio back in.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- Engine ---
// Was four interlocking checkboxes resolved by nested ba.if; the window
// is one exclusive choice, and engaging the engine is a separate toggle.
engage   = vgroup("Engine", button("Engage[idx:0][tooltip: Runs the incoming signal through the reverse buffer for as long as it is held.]"));
window   = vgroup("Engine", nentry("Window[idx:1][style:radio{'1/8':0;'1/4':1;'1/2':2;'1 Bar':3}][tooltip: Reverse window size, from rapid backward glitch repeats to a standard one-bar phrase reversal.]", 0, 0, 3, 1));

// Request Profile: Runs 100% wet by default
outputMix = vgroup("Mix", hslider("Dry/Wet[idx:2][unit:%][tooltip: Blends your original clean signal back into the 100% wet reversed texture.]", 100.0, 0.0, 100.0, 1.0)) / 100.0;

process(left, right) = wetL, wetR
with {
    engineActive = engage > 0.0;

    loopWindow = ba.selectn(4, window, 12000, 24000, 48000, 96000);

    globalClock = (+ (1) ~ _);

    reverseEngine(sig) = select2(engineActive == 0.0, reversedAudio, sig)
    with {
        intRamp = globalClock % loopWindow;

        // Reverse playback means reading progressively further back on each
        // sample, so the delay has to grow across the window and reset at
        // the boundary.
        //
        // The original built the delay from writePointer (globalClock %
        // 400000), treating a buffer index as a delay amount. That made the
        // delay grow without bound, so the engine read seconds into the
        // past instead of the last window: silence at the start of a take,
        // and a jump every time the clock wrapped. Its `tapeMemory` write
        // was dead code, never read by anything.
        reversedAudio = sig @ (500 + intRamp);
    };

    spLeft  = reverseEngine(left);
    spRight = reverseEngine(right);

    wetL = (left  * (1.0 - outputMix)) + (spLeft  * outputMix);
    wetR = (right * (1.0 - outputMix)) + (spRight * outputMix);
};
