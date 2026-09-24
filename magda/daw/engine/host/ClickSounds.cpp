#include "ClickSounds.hpp"

#include "transport/ClickGenerator.hpp"

namespace magda::daw::engine_host {

juce::AudioBuffer<float> renderClickSound(bool accent, double sampleRate) {
    return engine::ClickGenerator::renderSound(accent, sampleRate);
}

}  // namespace magda::daw::engine_host
