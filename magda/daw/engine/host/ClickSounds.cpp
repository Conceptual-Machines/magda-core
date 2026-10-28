// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClickSounds.hpp"

#include "transport/ClickGenerator.hpp"

namespace magda::daw::engine_host {

juce::AudioBuffer<float> renderClickSound(bool accent, double sampleRate) {
    return engine::ClickGenerator::renderSound(accent, sampleRate);
}

}  // namespace magda::daw::engine_host
