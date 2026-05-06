declare name "MagdaGranularDelay";
declare description "Stereo granular delay — feedback delay line read through a 4-voice windowed grain bank with pitch shift and position jitter.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Free-time delay (used when sync is off). Greyed when Sync (slot 2) is ON.
time      = hslider("Time [unit:ms] [idx:0] [gate:!2]", 500, 1, 2000, 1)
            : si.smooth(ba.tau2pole(0.05));

// Note division when synced. Same encoding as magda_delay.
division  = nentry("Division [idx:1] [gate:2] [style:menu{
                    '1/16T':0.16667;
                    '1/16':0.25;
                    '1/8T':0.33333;
                    '1/8':0.5;
                    '1/8.':0.75;
                    '1/4T':0.66667;
                    '1/4':1.0;
                    '1/4.':1.5;
                    '1/2T':1.33333;
                    '1/2':2.0;
                    '1/2.':3.0;
                    '1/1':4.0
                  }]", 1.0, 0.125, 4.0, 0.001);

sync      = checkbox("Sync [idx:2]");

// Grain length in milliseconds. Sub-30ms gets glitchy on purpose.
size_ms   = hslider("Size [unit:ms] [idx:3]", 120, 20, 500, 1)
            : si.smooth(ba.tau2pole(0.05));

// Pitch shift in semitones. Each grain is read at the corresponding rate
// inside its window, so positive values cover more buffer per grain
// (chipmunk), negative values cover less (slowed-down tail).
pitch_st  = hslider("Pitch [unit:st] [idx:4]", 0, -24, 24, 0.01)
            : si.smooth(ba.tau2pole(0.05));

// Per-grain position jitter, as a fraction of grain length. 0 = grains
// always read from the same buffer offset; 1 = grain center wanders by
// up to ±size_ms.
spray     = hslider("Spray [idx:5]", 0.0, 0.0, 1.0, 0.001);

feedback  = hslider("Feedback [idx:6]", 0.30, 0.0, 0.95, 0.001);
mix       = hslider("Mix [idx:7]", 0.40, 0.0, 1.0, 0.001);

// Hidden host-driven BPM. See magda_delay for the role:projectTempo
// plumbing — host writes the live edit tempo into this slot's zone
// every audio block.
bpm       = nentry("BPM [role:projectTempo] [hidden:1] [idx:63]",
                   120.0, 20.0, 999.0, 0.001);

// ============================================================================
// DSP
// ============================================================================

MAX_DELAY = 192000;     // ~4 s at 48 kHz, plus headroom for higher SRs.
NVOICES   = 4;          // 25%-overlap Hann grains per channel.

// Delay-time selection (free vs. sync) — same as magda_delay.
syncedSamples = division * 60.0 / max(bpm, 1.0) * ma.SR;
freeSamples   = time * ma.SR / 1000.0;
delaySamples  = (1.0 - sync) * freeSamples + sync * syncedSamples;

// Grain length (samples) and pitch ratio.
grainSamples  = size_ms * ma.SR / 1000.0;
pitchRatio    = pow(2.0, pitch_st / 12.0);

// Hann envelope — 0 at edges, 1 at center.
hann(x) = 0.5 - 0.5 * cos(2.0 * ma.PI * x);

// One grain voice. `x` is the buffered (already-delayed-and-fed-back)
// signal; we read it at a per-voice time-varying offset inside the
// grain window, multiply by the Hann envelope, and emit.
//
// fdelay reads `x(t - readOffset)`, so to play back the grain at a
// pitch ratio r the readOffset must drift at rate (1 - r) per sample
// of real time:
//   r = 1   → readOffset constant     → unity-rate playback (a normal delay)
//   r > 1   → readOffset shrinks      → faster playback (chipmunk)
//   r < 1   → readOffset grows        → slower playback (smear)
// (The intuitive `* pitchRatio` slope is wrong: it makes readOffset
// drift in lockstep with t at r=1, freezing the grain on a single
// buffer sample so the only audible content is Hann-shaped DC.)
//
// p          : 0..1 phasor at one cycle per grainSamples, offset by k/N
// trig       : 1 at the moment p wraps (new grain start) — used to S&H
//              the position jitter so it stays constant across the grain
// posHold    : sample-and-held jitter, in samples
// readOffset : centred on delaySamples at p=0.5; (p - 0.5) sweeps
//              ±0.5·grainSamples·(1 - pitchRatio) around it.
voice(k, x) = (x : de.fdelay(MAX_DELAY, readOffset)) * hann(p)
with {
    rate       = ma.SR / max(grainSamples, 1.0);
    p          = (os.phasor(1.0, rate) + k / NVOICES) : ma.frac;
    trig       = (p < p') > 0.5;
    posHold    = ba.sAndH(trig, no.noise) * spray * grainSamples;
    readOffset = max(0.0,
                     delaySamples
                     + (p - 0.5) * grainSamples * (1.0 - pitchRatio)
                     + posHold);
};

// Mono granulator: split the input to NVOICES copies, run each through
// `voice(k)` with k = 0..N-1 (so the staggered phasors give 25%-overlap
// grains for N=4), sum back to mono. The 2/N gain compensates for
// Hann-window OLA: at unity pitch, sum of N evenly-phased Hann windows
// equals N/2, so 2/N restores unity gain.
oneGrainLine = _ <: par(k, NVOICES, voice(k)) :> *(2.0 / NVOICES);

// Cross-mix two recirculation taps for ping-pong-style stereo from a
// single shared `cross` (kept fixed at 0.5 here — gentle stereo coupling
// without an extra knob). cross=0 keeps each tap on its own side;
// cross=1 fully swaps L↔R.
xmix(a, b) = a * 0.5 + b * 0.5,
             b * 0.5 + a * 0.5;

// Stereo granular delay. Mirrors magda_delay.dsp's structure: a 2-in
// 2-out delay-line block fed back through a per-channel feedback gain
// and the cross-mixer. Each side's "delay line" is replaced with the
// grain bank, so the feedback path is read out as windowed grains.
stereoGrainDelay = (ro.interleave(2, 2)
                    : (+, +)
                    : (oneGrainLine, oneGrainLine))
                   ~ (xmix : par(i, 2, *(feedback)));

// Wet/dry mix. Same idiom as magda_delay.
process = _, _
        <: (_, _), stereoGrainDelay
        :  par(i, 2, *(1.0 - mix)), par(i, 2, *(mix))
        :> _, _;
