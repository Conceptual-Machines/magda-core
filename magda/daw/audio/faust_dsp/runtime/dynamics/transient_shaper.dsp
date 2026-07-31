declare name "Transient Shaper";
declare description "True Independent Velocity-Based Transient Designer: Zero cross-talk between attack and sustain loops.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// --- User Calibration Controls ---
attackGain  = vgroup("Attack", hslider("Boost/Cut[idx:0][unit:dB]", 0.0, -12.0, 12.0, 0.1));
attackSpeed = vgroup("Attack", hslider("Speed[idx:1][unit:ms]", 2.0, 0.5, 20.0, 0.1));

sustainGain = vgroup("Sustain", hslider("Boost/Cut[idx:2][unit:dB]", 0.0, -12.0, 12.0, 0.1));
sustainSpeed= vgroup("Sustain", hslider("Release[idx:3][unit:ms]", 50.0, 10.0, 300.0, 1.0));

// Master utility controls
outputGain  = vgroup("Output", hslider("Trim[idx:4][unit:dB]", 0.0, -12.0, 12.0, 0.1));
outputMix   = vgroup("Output", hslider("Master Mix[idx:5][unit:%]", 100.0, 0.0, 100.0, 1.0)) / 100.0;

process(left, right) = attach(clipL, shaperMeter), clipR
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

    // ========================================================================
    // 5. SHAPING READOUT
    // ========================================================================
    // How much gain the shaper is applying right now, in dB. The host polls
    // this about thirty times a second, so the hold is done here: at a 0.5 ms
    // Speed the envelope can spike and settle several times between two polls,
    // and an unheld value would simply miss it.
    gainDb = masterDynamicEnvelope : max(ma.EPSILON) : ba.linear2db;
    holdSamples = ba.sec2samp(0.25);
    // Boost and cut are held separately and the larger excursion wins, so the
    // sign survives the hold; peak-holding the magnitude alone would report a
    // cut as a boost.
    boostHold = gainDb : max(0.0) : ba.peakholder(holdSamples);
    cutHold = gainDb : min(0.0) : *(-1.0) : ba.peakholder(holdSamples);

    // vbargraph: the reading is a level, and a vertical bar spanning zero
    // grows up for a boost and down for a cut.
    shaperMeter = ba.if(boostHold >= cutHold, boostHold, 0.0 - cutHold)
      : vgroup("Output",
               vbargraph("Shaping [unit:dB] [tooltip:Gain the shaper is applying]",
                         -12.0, 12.0));
};
