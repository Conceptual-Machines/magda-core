#pragma once

/**
 * @file AudioIOControl.hpp
 * @brief What Audio Settings drives: which interface, and exactly which channels open (#2749).
 */

#include "../../core/Config.hpp"
#include "HardwareChannels.hpp"

namespace magda {

/**
 * @brief The audio interface as a choice Audio Settings makes and the engine keeps.
 *
 * AudioIOService under the native engine, an adapter over Tracktion's wave devices under
 * Tracktion.
 */
class AudioIOControl : public HardwareChannels {
  public:
    /** @brief What is chosen, including an interface chosen with no channels on it. */
    virtual AudioIOSettings chosen() const = 0;

    /** @brief Open exactly @p settings and keep them as the choice; returns the open error. */
    virtual juce::String apply(const AudioIOSettings& settings) = 0;

    /** @brief What @p interfaceName calls its channels, whether or not it is open. */
    virtual juce::StringArray channelNames(const juce::String& backend,
                                           const juce::String& interfaceName, bool inputs) = 0;
};

}  // namespace magda
