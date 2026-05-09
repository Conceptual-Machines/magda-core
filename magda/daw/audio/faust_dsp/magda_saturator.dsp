declare name "MagdaSaturator";
declare description "Multi-mode saturator: drive into nonlinearity with bias / tone tilt / wet-dry mix and output trim.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Input gain into the nonlinearity. 0 dB = nominal, 24 dB pushes hard.
// Heavily smoothed because automating drive otherwise zipper-distorts.
drive_db = hslider("Drive [unit:dB] [idx:0]", 0.0, 0.0, 24.0, 0.1)
           : si.smooth(ba.tau2pole(0.02));

// Nonlinearity flavor. Stateless, so every branch can run cheaply per
// sample — selectn fans out the math but only one waveshape is taken.
mode = nentry("Mode [idx:1] [style:menu{
                'Tanh':0;
                'Soft':1;
                'Hard':2;
                'Fold':3;
                'Tube':4;
                'Tape':5
              }]", 0, 0, 5, 1);

// DC offset injected before the nonlinearity. Pushes the signal off-axis
// so the negative and positive halves saturate asymmetrically — the source
// of even-order harmonics (tube character). The DC the bias creates is
// removed downstream by a dcblocker so the user only hears the harmonic
// colour, not a thump.
bias = hslider("Bias [idx:2]", 0.0, -1.0, 1.0, 0.001)
       : si.smooth(ba.tau2pole(0.02));

// Post-distortion tilt EQ. Same idiom as magda_delay's Tone — blend a
// 1 kHz LP and HP against the dry. 0 = flat, +1 = bright, -1 = dark.
tone = hslider("Tone [idx:3]", 0.0, -1.0, 1.0, 0.001);

// Parallel saturation: 0 = dry, 1 = fully wet. Useful for keeping
// transient detail while adding harmonic warmth.
mix = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001);

// Output trim after the saturator chain (wet only — keeps dry as reference).
output_db = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 6.0, 0.1)
            : si.smooth(ba.tau2pole(0.02));

// ============================================================================
// DSP
// ============================================================================

drive_lin  = drive_db  : ba.db2linear;
output_lin = output_db : ba.db2linear;

// --- Nonlinearity flavors -----------------------------------------------
// Each takes an already-driven sample and returns roughly [-1..1]. None
// of them have internal state — selectn evaluating all six per sample is
// only ~6× the per-sample arithmetic, which is negligible vs. the filter
// pack's 6× stateful filters.

tanh_nl(x) = ma.tanh(x);

// Polynomial soft-clip that turns into a hard-ish limit past |x|=1. Up
// to ~0.7 it's near-cubic (subtle harmonics); past that it asymptotes.
soft_nl(x) = select2(ma.fabs(x) < 1.0,
                     ma.signum(x) * (1.0 - exp(0.0 - ma.fabs(x))),
                     x - x*x*x / 3.0);

// Hard clip at ±1. Aliases more than the others — that's the point.
hard_nl(x) = max(-1.0, min(1.0, x));

// Sine wavefolder. |x|>1 wraps back instead of clipping, which gives the
// folded-FM / Buchla-ish bell-tone harmonic series.
fold_nl(x) = sin(x * ma.PI * 0.5);

// Asymmetric tube-style: positive half-cycle softer than negative. Pre-
// scaling per side gives 12AX7-ish even-harmonic colour even before the
// Bias control kicks in.
tube_nl(x) = select2(x > 0.0,
                     ma.tanh(x * 1.4),
                     ma.tanh(x * 1.0));

// Tape-style: tanh modulated by a mild odd-order term. Less aggressive
// than tanh alone, with a gentle compression-like flat-top.
tape_nl(x) = ma.tanh(x) * (1.0 - 0.15 * x * x);

nl(m, x) = ba.selectn(6, m,
                      tanh_nl(x), soft_nl(x), hard_nl(x),
                      fold_nl(x), tube_nl(x), tape_nl(x));

// --- Tone tilt -----------------------------------------------------------
// Same shape as magda_delay's tilt: blend dry against a fixed-frequency
// LP/HP pair. 1 kHz pivot is the conventional "tilt EQ" centre.
tone_tilt(x) = x * (1.0 - ma.fabs(tone))
             + (x : fi.lowpass (2, 1000.0)) * max(0.0, -tone)
             + (x : fi.highpass(2, 1000.0)) * max(0.0,  tone);

// --- Per-channel chain ---------------------------------------------------
// 1. Pre-scale by drive
// 2. Add bias DC
// 3. Run through selected nonlinearity
// 4. DC-block to remove the bias-induced offset (and any DC the asymmetric
//    flavors generate on their own)
// 5. Tilt-EQ
// 6. Apply output trim
//
// Wet is then crossfaded with the untouched dry input via Mix.
sat_chain(x) = (x * drive_lin + bias)
             : nl(int(mode))
             : fi.dcblockerat(20.0)
             : tone_tilt
             : *(output_lin);

channel(x) = (x * (1.0 - mix) + sat_chain(x) * mix) : soft_limit;

// Output safety net. Below the knee (~-1.4 dBFS) the signal passes through
// unchanged so quiet/normal material isn't coloured; above the knee a tanh
// curve soft-clips toward ±1.0 instead of slamming the converter. Catches
// the case where Drive + Output + Mode=Hard would otherwise push past
// 0 dBFS, without sneaking compression into low-level transients.
// Faust convention: select2(s, A, B) → A when s=0, B when s=1. So put the
// above-knee soft-clip branch FIRST (for |x| >= SOFT_KNEE, where the
// comparison is false) and the passthrough SECOND (|x| < SOFT_KNEE, true).
// Reversed order silently turns the limiter into a constant-amplitude
// generator that pushes any small signal up to ±0.7 — heard as a square
// wave at the input zero-crossings.
SOFT_KNEE = 0.85;
soft_limit(x) = select2(ma.fabs(x) < SOFT_KNEE,
                        ma.signum(x)
                          * (SOFT_KNEE
                             + (1.0 - SOFT_KNEE)
                               * ma.tanh((ma.fabs(x) - SOFT_KNEE)
                                         / (1.0 - SOFT_KNEE))),
                        x);

process = channel, channel;
