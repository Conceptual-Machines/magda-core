#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <magda/sdk/audio/BufferView.hpp>

namespace magda::engine {

/// Allocation-free view of @p block for the SDK taps; channels past the view's maximum are dropped.
inline ConstBufferView viewOf(juce::dsp::AudioBlock<const float> block) {
    std::array<const float*, ConstBufferView::kMaxChannels> channels{};
    const auto count =
        std::min(static_cast<int>(block.getNumChannels()), ConstBufferView::kMaxChannels);
    for (auto channel = 0; channel < count; ++channel)
        channels[static_cast<std::size_t>(channel)] =
            block.getChannelPointer(static_cast<std::size_t>(channel));
    return ConstBufferView(channels.data(), count, static_cast<int>(block.getNumSamples()));
}

}  // namespace magda::engine
