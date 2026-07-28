declare name "Realistic Rain Generator";
declare description "Advanced physical model using pitch-tracked high-resonance interpolated fractional comb filters for hollow river splashes, combined with an isolated stochastic hidden reverb puddle engine under Faust 2.70.3.";
declare author "mikobuntu";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

// ==========================================
// 1. MAGDA ENVIRONMENTAL UI CONTROLS
// ==========================================
intensity   = hslider("1. Rain Intensity", 0.4, 0.0, 1.0, 0.01);
roof_mix    = hslider("2. Tin Roof Impacts", 0.3, 0.0, 1.0, 0.01);
puddle_mix  = hslider("3. Puddle Splashes", 0.2, 0.0, 1.0, 0.01);
river_mix   = hslider("4. River/Deep Splashes", 0.1, 0.0, 1.0, 0.01);

// This single slider controls the entire hidden rain/reverb engine module
rainVol     = hslider("5. Puddle Drops + Hidden Reverb [%]", 20.0, 0.0, 100.0, 1.0) / 100.0;

// ==========================================
// 2. MATH UTILITIES & COMPILER-SAFE MACROS
// ==========================================
rand_engine = (+(12345) : *(1103515245)) ~ _ : /(2147483647.0);

fixed_decay(coeff, trig) = loop ~ _
with {
    loop(fb) = trig + (fb * coeff);
};

// Isolated DC offset blocker
dc_block(x) = x - x' + 0.995 * y ~ _ with { y(fb) = fb; };

// ==========================================
// 3. THE TEXTURED WEATHER LAYERS
// ==========================================

// --- Layer A: Constant Distant Rain Shimmer ---
distant_layer = rand_engine : fi.highpass(1, 1000.0) * 0.08 * (intensity * 0.7 + 0.3);

// --- Layer B: Erratic Tin Roof Impacts ---
roof_trig  = abs(rand_engine) > (0.997 - (intensity * 0.012));
roof_layer = roof_trig * rand_engine * roof_mix * 0.6;

// --- Layer C: Liquid Surface Puddle Splashes ---
bubble_trig    = abs(rand_engine) > (0.998 - (intensity * 0.005));
bubble_pulse   = fixed_decay(0.991, bubble_trig); 
dynamic_cutoff = 200.0 + (bubble_pulse * 1500.0); 
puddle_layer   = (bubble_pulse * rand_engine) : fi.lowpass(1, dynamic_cutoff) * puddle_mix * 0.4;

// --- Layer D: River Splashes (High-Resonance Interpolated Comb Engine) ---
noise_sample = abs(rand_engine);

// Sparse, completely unpredictable trigger pulses
splash_trig = noise_sample > (0.99988 - (intensity * 0.00015));

// Primary fluid volume envelope follower
river_env = fixed_decay(0.965, splash_trig);

// Lock a unique random baseline bubble pitch (1000 Hz to 2200 Hz) per droplet
base_pitch = ba.latch(splash_trig, 1000.0 + (noise_sample * 1200.0));

// Exponential upward pitch chirp tracking path
pitch_curve  = (1.0 - river_env) * (1.0 - river_env);
bubble_pitch = base_pitch + (pitch_curve * 500.0);

// Convert frequency smoothly into sample delay bounds
delay_samples = 48000.0 / bubble_pitch : min(256) : max(2);

// A 1-sample spike of white noise to cleanly kick-start the comb filter loop
impact_excitation = splash_trig * rand_engine * 0.45;

// FIXED FOR SILENT MODULATION: Replaced raw static delay (@) with de.fdelay.
comb_loop(fb) = impact_excitation + (de.fdelay(256, delay_samples, fb) * 0.992);
comb_core     = comb_loop ~ _;

// Clean out any subsonic mud, scale with the envelope, and apply a 12x boost
river_plop = comb_core : fi.highpass(1, 1100.0) * river_env * river_mix * 12.0;

// Dynamic sample-and-hold stereo panning allocation per individual drop
// By using an independent noise signal mapped from 0.0 to 1.0, 
// drops are guaranteed to scatter symmetrically between left and right channels.
random_pan = (no.noise + 1.0) * 0.5;
pan_L   = ba.latch(splash_trig, random_pan);
pan_R   = 1.0 - pan_L;

river_L = river_plop * pan_L;
river_R = river_plop * pan_R;

// --- Layer E: Integrated Stochastic RainPuddle Module (Hidden UI) ---
puddle_module_out = rain_dry + quietReverb
with {
    bubble_engine(f0,trig) = os.osc(f) * (exp(-damp*time) : si.smooth(0.99))
    with {
        damp = 0.043*f0 + 0.0014*f0^(3/2);
        f = f0*(1+sigma*time);
        sigma = eta * damp;
        eta = 0.075;
        time = 0 : (select2(trig>trig'):+(1)) ~ _ : ba.samp2sec;
    };

    // Slowed-down trigger configuration optimized for clear drops
    puddle_rate = 1.0;        
    min_f = 250.0;      
    max_f = 3500.0;     
    gamma = 5.0;        

    poisson_thresh = puddle_rate / ma.SR;
    puddle_noise1  = (no.noise + 1.0) * 0.5;
    puddle_trig    = puddle_noise1 < poisson_thresh;

    puddle_noise2  = (no.noise' + 1.0) * 0.5;
    rand_latch     = puddle_noise2 : ba.sAndH(puddle_trig);
    shape          = 1.0 / (gamma + 1.0);
    power_rand     = 1.0 - (rand_latch ^ shape);
    puddle_freq    = min_f + (power_rand * (max_f - min_f));

    // The dry rain generator output scaled by the master slider
    rain_dry = bubble_engine(puddle_freq, puddle_trig) * rainVol;

    // Stable 1-in 1-out Freeverb pipeline running quietly in the background
    // (RoomSize=0.16, fb2=0.0337, Damp=1.0, Spread=441) * 0.15 Return Level
    quietReverb = rain_dry : re.mono_freeverb(0.16, 0.0337, 1.0, 441) * 0.15;
};

// ==========================================
// 4. OUTPUT CEILING & SIGNAL MIX MATRIX
// ==========================================
clipper(sig) = max(-0.988, min(0.988, sig));

// Symmetrical Stereo Multipliers for the background textures (No phase inversion)
distant_L = distant_layer * 0.7;
distant_R = distant_layer * 0.7;

roof_L    = roof_layer * 0.6;
roof_R    = roof_layer * 0.6;

puddle_L  = puddle_layer * 0.5;
puddle_R  = puddle_layer * 0.5;

// The main processing matrix now sums identical volume weights to both sides
rain_L_calc = clipper((distant_L + roof_L + puddle_L + river_L + puddle_module_out) * 0.4) : dc_block;
rain_R_calc = clipper((distant_R + roof_R + puddle_R + river_R + puddle_module_out) * 0.4) : dc_block;

process(in_L, in_R) = clipper(in_L + rain_L_calc), clipper(in_R + rain_R_calc);

