declare name "Vinyl Emulator";
declare description "A vinyl texture and pitch-instability emulator featuring a 45 RPM pitch wow engine, algorithmic micro-jitter pop/crackle generation, and a dedicated transient high-pass filter.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

//import("maths.lib");
import("stdfaust.lib");
// --- UI Controls ---
vinylNoiseOn = checkbox("1. Engage 45 RPM Vinyl Mode [Off/On]");
wowDepth     = hslider("2. Turntable Warp Depth [%]", 25.0, 0.0, 100.0, 1.0) / 100.0;
crackleGain  = hslider("3. Vinyl Crackle Volume [dB]", 0.0, -24.0, 12.0, 0.1);

process(left, right) = wetL, wetR
with {
    // --- 1. OVERFLOW-SAFE FREE-RUNNING JITTER ENGINE ---
    // Two fast phase accumulators running at slightly different speeds.
    // They loop continuously between 0.0 and 1.0, making them completely safe from overflows.
    phaseA = (+ (0.0137) : fmod(_, 1.0)) ~ _;
    phaseB = (+ (0.0079) : fmod(_, 1.0)) ~ _;

    // Microscopic jitter is captured by multiplying the out-of-phase waveforms
    jitterStream = phaseA * phaseB;

    // --- 2. RANDOM BURST TRIGGERS ---
    // We check for sharp mathematical boundaries to generate short, random impulse spikes
    crackleTrig = fmod(jitterStream * 1000.0, 1.0) > 0.985;
    popTrig     = fmod(jitterStream * 5341.0, 1.0) > 0.996;

    // Turn the triggers into raw audio impulses
    rawCrackle  = crackleTrig * jitterStream;
    rawPop      = popTrig     * jitterStream * 2.5;

    // --- 3. STRICT HIGH-PASS FILTER (REMOVES DRONES & LOW END) ---
    // A 1-pole high-pass filter that strips out all low frequencies and drones.
    // It keeps only the bright, high-frequency transients above 8,000 Hz.
    highPass(s) = filter ~ _
    with {
        filter(lastY) = s - (lastY + 0.1 * (s - lastY));
    };

    // --- 4. COMBINED VINYL TEXTURE LAYER ---
    // Combine the pops and crackles, run them through the high-pass filter, and apply gain
    noiseGain  = pow(10.0, crackleGain / 20.0);
    vinylLayer = (rawCrackle + rawPop) : highPass : * (0.15 * noiseGain);

    // --- 5. 45 RPM TURNTABLE WOW & FLUTTER ---
    turntableWarp(audioIn) = select2(vinylNoiseOn == 0.0, warpedSignal, audioIn)
    with {
        lfoPhase = (+ (4.5 / 48000.0) : fmod(_, 1.0)) ~ _;
        lfo      = sin(lfoPhase * 2.0 * 3.14159265);
        modIndex = max(0, min(120, int(60.0 + (lfo * wowDepth * 60.0))));
        warpedSignal = audioIn @ modIndex;
    };

    // --- 6. EXPLICIT GATED ROUTING MATRIX ---
    wetL = turntableWarp(left)  + (vinylLayer * vinylNoiseOn);
    wetR = turntableWarp(right) + (vinylLayer * vinylNoiseOn);
};
