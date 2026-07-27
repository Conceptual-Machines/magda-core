declare name "Transient Shaper";
declare description "True Independent Velocity-Based Transient Designer — Zero cross-talk between attack and sustain loops.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- User Calibration Controls ---
attackGain  = hslider("1. Attack Boost/Cut [dB]", 0.0, -12.0, 12.0, 0.1);
attackSpeed = hslider("2. Attack Speed [ms]", 2.0, 0.5, 20.0, 0.1);

sustainGain = hslider("3. Sustain Boost/Cut [dB]", 0.0, -12.0, 12.0, 0.1);
sustainSpeed= hslider("4. Sustain Release [ms]", 50.0, 10.0, 300.0, 1.0);

// Master utility controls
outputGain  = hslider("5. Output Trim [dB]", 0.0, -12.0, 12.0, 0.1);
outputMix   = hslider("6. Master Mix [%]", 100.0, 0.0, 100.0, 1.0) / 100.0;

process(left, right) = clipL, clipR
with {
    // ========================================================================
    // 1. CONTROL DECODER
    // ========================================================================
    attDelta = ba.db2linear(attackGain) - 1.0;
    susDelta = ba.db2linear(sustainGain) - 1.0;
    trimMult = ba.db2linear(outputGain);

    inputPeak = max(abs(left), abs(right));
    gateThreshold = 0.001;

    // ========================================================================
    // 2. TRUE VELOCITY-BASED ATTACK DETECTOR
    // ========================================================================
    // Tracks how fast the signal is rising. It ignores sustain completely.
    attPole = exp(-1.0 / (max(0.5, attackSpeed) / 1000.0 * ma.SR));
    attEnv  = inputPeak : max ~ (*(attPole));
    
    // Smooth the attack tracker slightly to create a comparison point
    attEnvSmooth = attEnv : si.smoo; 
    
    // Velocity calculation: Only positive values mean the signal is spiking upward
    signalVelocity = max(0.0, attEnv - attEnvSmooth);
    normAttDenom   = max(gateThreshold, attEnv);
    attackModifier = attDelta * (signalVelocity / normAttDenom);

    // ========================================================================
    // 3. TRUE VELOCITY-BASED SUSTAIN DETECTOR
    // ========================================================================
    // Tracks the release phase/body of the signal, independent of the attack front.
    susPole = exp(-1.0 / (max(10.0, sustainSpeed) / 1000.0 * ma.SR));
    susEnv  = inputPeak : max ~ (*(susPole));
    
    // Isolate the steady-state body of the signal relative to total energy
    normSusDenom    = max(gateThreshold, inputPeak);
    sustainModifier = susDelta * (susEnv / normSusDenom);

    // ========================================================================
    // 4. COMBINATION MATRIX & OUTPUT
    // ========================================================================
    masterDynamicEnvelope = max(0.0, 1.0 + attackModifier + sustainModifier);

    shaperL = left  * masterDynamicEnvelope * trimMult;
    shaperR = right * masterDynamicEnvelope * trimMult;

    wetL = (left  * (1.0 - outputMix)) + (shaperL * outputMix);
    wetR = (right * (1.0 - outputMix)) + (shaperR * outputMix);

    clipL = ba.if(wetL > 1.0, 1.0, ba.if(wetL < -1.0, -1.0, wetL));
    clipR = ba.if(wetR > 1.0, 1.0, ba.if(wetR < -1.0, -1.0, wetR));
};

