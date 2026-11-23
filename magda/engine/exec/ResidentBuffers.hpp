// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>

namespace magda::engine {

/** @brief Sizes @p buffer and writes every sample, so its pages are mapped before a render
 *  thread first touches them (#3402). A cleared allocation is calloc'd and stays unmapped. */
inline void setSizeResident(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) {
    buffer.setSize(numChannels, numSamples, false, true, false);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::clear(buffer.getWritePointer(channel), buffer.getNumSamples());
}

/** @brief Reserves @p bytes in an empty @p buffer and writes them, for the same reason. */
inline void ensureSizeResident(juce::MidiBuffer& buffer, std::size_t bytes) {
    buffer.ensureSize(bytes);
    if (!buffer.data.isEmpty() || bytes == 0)
        return;
    buffer.data.insertMultiple(0, 0, static_cast<int>(bytes));
    buffer.data.clearQuick();
}

}  // namespace magda::engine
