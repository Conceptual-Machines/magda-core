#pragma once

#include <algorithm>
#include <vector>

#include "audio/plugins/MagdaDevice.hpp"

namespace magda::test {

/**
 * @brief An sdk::ProcessContext over test-owned juce buffers.
 *
 * Owns the channel-pointer arrays the views hold, so it is neither copied nor moved: build
 * one, set the fields a case needs on @ref context, and hand that to the device.
 */
class DeviceTestBlock {
  public:
    /// The block is @p numSamples frames of @p audio from @p startSample; by default all of it.
    explicit DeviceTestBlock(juce::AudioBuffer<float>& audio, int numSamples = -1,
                             int startSample = 0) {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            audioChannels_.push_back(audio.getWritePointer(channel) + startSample);
        context.audio =
            BufferView(audioChannels_.data(), audio.getNumChannels(),
                       numSamples >= 0 ? numSamples : audio.getNumSamples() - startSample);
    }

    DeviceTestBlock(const DeviceTestBlock&) = delete;
    DeviceTestBlock& operator=(const DeviceTestBlock&) = delete;

    /// The key, read-only, with as many frames as the block.
    void setSidechain(const juce::AudioBuffer<float>& key) {
        keyChannels_.assign(key.getArrayOfReadPointers(),
                            key.getArrayOfReadPointers() + key.getNumChannels());
        context.sidechain =
            ConstBufferView(keyChannels_.data(), key.getNumChannels(), context.audio.numFrames());
    }

    sdk::ProcessContext context;

  private:
    std::vector<float*> audioChannels_;
    std::vector<const float*> keyChannels_;
};

}  // namespace magda::test
