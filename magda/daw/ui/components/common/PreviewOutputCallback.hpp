// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>

#include "core/Config.hpp"

namespace magda {

/**
 * @brief Plays a preview source on the stereo pair Config names for previews.
 *
 * Shared by the media browser and the capture panel, so every preview goes where the
 * user sent previews. The pair is read each callback, so a settings change is heard
 * at once; reading it takes no lock.
 */
class PreviewOutputCallback : public juce::AudioIODeviceCallback {
  public:
    void setSource(juce::AudioIODeviceCallback* source) {
        source_ = source;
    }

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData,
        int numOutputChannels, int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {
        if (source_ == nullptr) {
            // Zero all output channels
            for (int i = 0; i < numOutputChannels; ++i)
                if (outputChannelData[i] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
            return;
        }

        // Read offset from Config so changes in the Audio & MIDI settings take effect immediately.
        // Config::getPreviewOutputChannel() is a plain member read — no locks or allocations.
        int offset = Config::getInstance().getPreviewOutputChannel();
        offset = std::max(offset, 0);

        // Check that the requested stereo pair fits within the available output channels
        if (offset + 1 < numOutputChannels) {
            // Build a 2-channel pointer array pointing at the target stereo pair
            float* stereoOut[2] = {outputChannelData[offset], outputChannelData[offset + 1]};

            // Let the source write into just these 2 channels
            source_->audioDeviceIOCallbackWithContext(inputChannelData, numInputChannels, stereoOut,
                                                      2, numSamples, context);

            // Zero every other output channel
            for (int i = 0; i < numOutputChannels; ++i) {
                if (i != offset && i != offset + 1 && outputChannelData[i] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
            }
        } else {
            // Offset out of range — write to first stereo pair as fallback
            float* stereoOut[2] = {outputChannelData[0],
                                   numOutputChannels > 1 ? outputChannelData[1] : nullptr};
            int chCount = numOutputChannels > 1 ? 2 : 1;
            source_->audioDeviceIOCallbackWithContext(inputChannelData, numInputChannels, stereoOut,
                                                      chCount, numSamples, context);

            // Zero remaining channels
            for (int i = chCount; i < numOutputChannels; ++i)
                if (outputChannelData[i] != nullptr)
                    juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        if (source_ != nullptr)
            source_->audioDeviceAboutToStart(device);
    }

    void audioDeviceStopped() override {
        if (source_ != nullptr)
            source_->audioDeviceStopped();
    }

  private:
    juce::AudioIODeviceCallback* source_ = nullptr;
};

}  // namespace magda
