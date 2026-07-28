declare name "MagdaTremolo";
declare description "Stage 1 Faust POC effect: stereo tremolo (sine LFO).";

import("stdfaust.lib");

rate  = hslider("Rate [unit:Hz]", 4.0,  0.1, 20.0, 0.01);
depth = hslider("Depth",          0.5,  0.0, 1.0,  0.01);

lfo = (1.0 - depth) + depth * (0.5 + 0.5 * os.osc(rate));

process = *(lfo), *(lfo);
