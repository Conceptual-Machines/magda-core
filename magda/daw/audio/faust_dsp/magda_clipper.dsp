declare name "MagdaClipper";
declare description "Multi-mode antialiased clipper. Six static nonlinearity shapes from the aa.* ADAA library.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

drive    = hslider("Drive [unit:dB] [idx:0]", 0.0, 0.0, 24.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));
mode     = nentry("Mode [idx:1] [style:menu{'Hard':0;'Soft':1;'Tanh':2;'Hyperbolic':3;'Sine':4;'Cubic':5}]",
                  0, 0, 5, 1);
outputDb = hslider("Output [unit:dB] [idx:2]", 0.0, -24.0, 12.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));
autogain = nentry("Autogain [idx:3] [style:menu{'Off':0;'On':1}]", 1, 0, 1, 1);

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);
driveLin = db2lin(drive);

// All six shapes come from aa.lib (antiderivative-antialiased). They are
// static nonlinearities — no envelope, no attack/release. Each is dirt
// cheap to evaluate, so Pattern A (run all, select one) is fine here.
clipHard(x)  = aa.hardclip(x);
clipSoft(x)  = aa.softclipQuadratic1(x);
clipTanh(x)  = aa.tanh1(x);
clipHyper(x) = aa.hyperbolic(x);
clipSine(x)  = aa.sinarctan(x);
clipCubic(x) = aa.cubic1(x);

clipper(x) = clipHard(x), clipSoft(x), clipTanh(x),
             clipHyper(x), clipSine(x), clipCubic(x)
             : ba.selectn(6, int(mode));

// Autogain: when on, pull -drive dB out of the output stage so cranking
// Drive changes character without changing perceived loudness. Default is
// On — the natural use-case for a clipper is "push into the curve",
// auto-compensated.
autogainDb = autogain * (-drive);

processOne(x) = (x * driveLin : clipper) * db2lin(outputDb + autogainDb);

process = processOne, processOne;
