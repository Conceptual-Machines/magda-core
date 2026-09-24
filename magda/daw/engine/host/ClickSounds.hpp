#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace magda::daw::engine_host {

/// Native's metronome click, accent or beat, rendered whole at unity gain (#2802).
juce::AudioBuffer<float> renderClickSound(bool accent, double sampleRate);

}  // namespace magda::daw::engine_host
