declare name "Drive";
declare magda_view "MagdaDrive";
declare description "Stage 1 Faust POC effect: drive + lowpass + gain, stereo.";
declare license "GPL-3.0";
declare version "1.0";

import("stdfaust.lib");

drive  = hslider("Drive [unit:dB]",  0.0,   0.0,   24.0,    0.1) : ba.db2linear;
cutoff = hslider("Cutoff [unit:Hz]", 20000, 200.0, 20000.0, 1.0);
gain   = hslider("Gain [unit:dB]",   0.0,  -24.0,  6.0,     0.1) : ba.db2linear;

channel = *(drive) : ma.tanh : fi.lowpass(2, cutoff) : *(gain);

process = channel, channel;
