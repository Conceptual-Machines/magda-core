declare name "MagdaBitcrusher";
declare description "Sample-rate and bit-depth reduction lo-fi crusher with input drive and a post-crush tone filter.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Target sample rate in Hz. Log scale because the perceptually useful
// range is 100 Hz to ~10 kHz; above 10 kHz the effect is subtle at typical
// host rates.
targetSR = hslider("Rate [unit:Hz] [scale:log] [scaleAnchor:4000] [idx:0]",
                   8000, 100, 48000, 1) : si.smooth(ba.tau2pole(0.02));

// Quantization bit depth. Integer 1..16. No smoothing - integer steps,
// changes are deliberate.
bits = hslider("Bits [idx:1]", 8, 1, 16, 1);

// Pre-quantization drive in dB. Pushing the input harder before
// quantization shifts where the bit boundaries land, giving a different
// crushed character even at the same bit depth.
driveDb = hslider("Drive [unit:dB] [idx:2]", 0.0, 0.0, 24.0, 0.1)
          : si.smooth(ba.tau2pole(0.02));

// Post-crush low-pass cutoff. Tames the aliasing and step harshness.
// Default 20 kHz means essentially off; lowering it gives a smoother,
// more "lo-fi cassette" character.
toneHz = hslider("Tone [unit:Hz] [scale:log] [scaleAnchor:2000] [idx:3]",
                 20000, 200, 20000, 1) : si.smooth(ba.tau2pole(0.02));

mix = hslider("Mix [idx:4]", 1.0, 0.0, 1.0, 0.001)
      : si.smooth(ba.tau2pole(0.02));

outDb = hslider("Output [unit:dB] [idx:5]", 0.0, -24.0, 12.0, 0.1)
        : si.smooth(ba.tau2pole(0.02));

// ============================================================================
// DSP
// ============================================================================

driveLin = pow(10.0, driveDb / 20.0);
outLin   = pow(10.0, outDb / 20.0);

// Sample-and-hold every N samples, where N = SR / targetSR. Clamp to at
// least 1 sample of hold so N=1 means "no downsampling".
holdSamples = max(1, int(ma.SR / max(targetSR, 1.0)));
downsample(x) = ba.sAndH(ba.pulse(holdSamples), x);

// Mid-tread quantization to 2^(bits-1) levels per side. Clamp the input
// so post-drive overshoot still maps to the top quantization step rather
// than wrapping.
levels(b) = pow(2.0, b - 1.0);
quantize(x) = floor(clamped * L + 0.5) / L
with {
    L = levels(bits);
    clamped = max(-1.0, min(1.0, x));
};

// Post-crush 1-pole low-pass for tone shaping.
tone = fi.lowpass(1, toneHz);

crush(x) = (x * driveLin) : downsample : quantize : tone;

dryWetMix(d, w) = (1.0 - mix) * d + mix * w;

process(l, r) = wL, wR
with {
    wL = dryWetMix(l, crush(l)) * outLin;
    wR = dryWetMix(r, crush(r)) * outLin;
};
