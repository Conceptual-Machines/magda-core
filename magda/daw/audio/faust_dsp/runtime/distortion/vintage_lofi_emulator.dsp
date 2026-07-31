declare name "Vintage Lofi Emulator";
declare description "A multi-mode vintage sampler emulator featuring downsampling, pitch-dependent clock jitter/ringing emulation, and tailored 10-bit/12-bit quantization schemes. Instructions: Select a sampler mode to engage crushing. Adjust the Lo-Fi FX Gain to amplify the character texturing independently of the master clean pass.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- Sampler model: one exclusive selection, 0 is clean bypass ---
// Was five interlocking checkboxes resolved by a priority encoder; the
// host renders an exclusive selector directly from [style:radio].
mode = vgroup("Sampler", nentry("Model[idx:0][style:radio{'Off':0;'SP-1200':1;'ASR-10':2;'W-30':3;'Lo-Fi 1':4;'Lo-Fi 2':5}]", 0, 0, 5, 1));

// --- Target Gain Parameter ---
lofiGain = vgroup("Output", hslider("Lo-Fi FX Gain[idx:1][unit:dB][tooltip: Amplifies the output level of the lo-fi engine texturing independently of the dry signal.]", 0.0, -12.0, 12.0, 0.1)) : ba.db2linear;

// --- Hard Clipper Master Toggle ---
clipToggle = vgroup("Output", checkbox("Master Hard Clipper[idx:2][tooltip: Strictly clamps output peaks between -1.0 and 1.0 to prevent host channel clipping.]"));

process(left, right) = outL, outR
with {
    // 1. Exact Integer Clock Period Calculation Based on a 48000 Hz Host Rate
    staticPeriod = select2(mode == 5,
                    select2(mode == 4,
                        select2(mode == 3,
                            select2(mode == 2, 1, 2),
                        3),
                    2),
                 4);

    // 3. SP-1200 Built-In Transient Ringing Emulation
    sp1200ClockLoop = loop ~ _
    with {
        loop(lastPhase) = select2(nextPhase >= 1.0, nextPhase, nextPhase - 1.0)
        with {
            nextPhase = lastPhase + (14000.0 / 48000.0);
        };
    };
    sp1200Trigger = sp1200ClockLoop < (sp1200ClockLoop : mem);

    // 4. Stable Countdown Clock Grid
    countDown = loop ~ _
    with {
        loop(lastCount) = select2(lastCount <= 0, lastCount - 1, staticPeriod - 1);
    };

    // 5. Unified Master Clock Trigger Generator
    clockTrigger = select2(mode == 0,
                    select2(mode == 1,
                        countDown == 0,
                        sp1200Trigger
                    ),
                 1.0);

    // 6. Zero-Keyword Pure Math Sample & Hold Latch
    sampleAndHold(audioIn) = latchLoop ~ _
    with {
        latchLoop(lastValue) = (audioIn * clockTrigger) + (lastValue * (1.0 - clockTrigger));
    };

    // 7. Dynamic Converter Bit-Depth Crunch Matrix
    quantizationLevels = select2(mode == 5,
                            select2(mode == 4,
                                select2(mode == 3,
                                    select2(mode == 2,
                                        select2(mode == 1, 1.0, 4095.0),
                                    1023.0),
                                4095.0),
                            1023.0),
                         4095.0);

    bitCrush(sig) = select2(mode == 0, int(sig * quantizationLevels) / quantizationLevels, sig);

    // 8. PARALLEL ARTIFACT ISOLATION MATRIX
    processedL = sampleAndHold(left)  : bitCrush;
    processedR = sampleAndHold(right) : bitCrush;

    artifactL = processedL - left;
    artifactR = processedR - right;

    wetL = left  + (artifactL * lofiGain);
    wetR = right + (artifactR * lofiGain);

    // 9. Automatic Clean Bypass Gate
    anyModeActive = mode > 0;
    finalL = select2(anyModeActive, left, wetL);
    finalR = select2(anyModeActive, right, wetR);

    // 10. MASTER CEILING SAFETY PATHWAY
    // Explicit, sample-accurate brickwall limiter function
    hardClip(x) = ba.if(x > 1.0, 1.0, ba.if(x < -1.0, -1.0, x));

    // Routes audio either cleanly or through the clipper depending on the toggle button
    outL = select2(clipToggle, finalL, hardClip(finalL));
    outR = select2(clipToggle, finalR, hardClip(finalR));
};
