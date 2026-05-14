declare name "MagdaClipper";
declare description "Multi-mode antialiased clipper. Five static nonlinearity shapes from the aa.* ADAA library.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

drive    = hslider("Drive [unit:dB] [idx:0]", 0.0, 0.0, 24.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));
mode     = nentry("Mode [idx:1] [style:menu{'Hard':0;'Soft':1;'Tanh':2;'Hyperbolic':3;'Sine':4}]",
                  0, 0, 4, 1);
outputDb = hslider("Output [unit:dB] [idx:2]", 0.0, -24.0, 12.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));

// ============================================================================
// DSP
// ============================================================================

db2lin(db) = pow(10.0, db / 20.0);
driveLin = db2lin(drive);

// All five shapes come from aa.lib (antiderivative-antialiased). They are
// static nonlinearities — no envelope, no attack/release. Each is dirt
// cheap to evaluate, so Pattern A (run all, select one) is fine here.
clipHard(x)  = aa.hardclip(x);
clipSoft(x)  = aa.softclipQuadratic1(x);
clipTanh(x)  = aa.tanh1(x);
clipHyper(x) = aa.hyperbolic(x);
clipSine(x)  = aa.sinarctan(x);

clipper(x) = clipHard(x), clipSoft(x), clipTanh(x),
             clipHyper(x), clipSine(x)
             : ba.selectn(5, int(mode));

processOne(x) = clipper(x * driveLin) * db2lin(outputDb);

process = processOne, processOne;
