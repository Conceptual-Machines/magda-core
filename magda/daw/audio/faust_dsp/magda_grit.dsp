declare name "MagdaGrit";
declare description "Bandpass-filtered noise / sine modulator: ring-modulates the input with a tone or filtered-noise carrier for Erosion-style texture.";

import("stdfaust.lib");

// ============================================================================
// User controls
// ============================================================================

// Carrier centre frequency. Doubles as the bandpass centre in the noise
// modes and the oscillator pitch in sine mode.
freq = hslider("Frequency [unit:Hz] [scale:log] [scaleAnchor:1000] [idx:0]",
               1000, 20, 16000, 1)
       : si.smooth(ba.tau2pole(0.02));

// Bandpass Q in the noise modes. Maps 0..1 to roughly Q = 0.5..20.
// No audible effect in sine mode (the carrier is already monotone).
width = hslider("Width [idx:1]", 0.5, 0.0, 1.0, 0.001)
        : si.smooth(ba.tau2pole(0.02));

// Modulation depth. 0 = bypass, 1 = full ring-modulated wet added to dry.
amount = hslider("Amount [idx:2]", 0.0, 0.0, 1.0, 0.001)
         : si.smooth(ba.tau2pole(0.02));

// Carrier source.
// - Noise: shared mono band-passed noise on both channels
// - Wide Noise: decorrelated noise per channel (stereo width)
// - Sine: tonal carrier at `freq` — more pitched / metallic artefact
mode = nentry("Mode [idx:3] [style:menu{
                'Noise':0;
                'Wide Noise':1;
                'Sine':2
              }]", 0, 0, 2, 1);

// ============================================================================
// DSP
// ============================================================================

// Q grows from a gentle slope (0.5) at width=0 to a sharp ringing band (20)
// at width=1.
q = 0.5 + width * 19.5;

// Independent noise sources for stereo decorrelation. no.multinoise(N)
// returns a bus of N white-noise channels with different seeds.
noise_mono = no.noise;
noise_l    = no.multinoise(2) : _, !;
noise_r    = no.multinoise(2) : !, _;

// Resonant bandpass at the carrier centre. fi.resonbp(fc, q, gain) — gain=1
// keeps unity at resonance; the broadband level drops as Q rises, which
// Amount compensates for in practice.
bpf(x) = x : fi.resonbp(freq, q, 1.0);

// Per-channel carrier. Sine carrier ignores the BPF since the filter has
// nothing to do on a single tone.
carrier_l = ba.selectn(3, int(mode),
                       bpf(noise_mono),
                       bpf(noise_l),
                       os.osc(freq));

carrier_r = ba.selectn(3, int(mode),
                       bpf(noise_mono),
                       bpf(noise_r),
                       os.osc(freq));

// Ring-modulate, scale by Amount, sum back into dry. Equivalent to
//   out = dry * (1 + amount² * carrier)
// Squaring `amount` gives the knob a perceptual taper — near zero you
// get a tiny grit smear (amount = 0.1 → 0.01 wet), and the effect
// ramps into the audible range only as the knob crosses ~0.3. Without
// the taper the bottom 10% of travel is already "too much" because
// the carrier × dry product is loud relative to the dry passthrough.
modulate(c, x) = x + amount * amount * (x * c);

process = modulate(carrier_l), modulate(carrier_r);
