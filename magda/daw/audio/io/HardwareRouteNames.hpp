#pragma once

/**
 * @file HardwareRouteNames.hpp
 * @brief Hardware channels under the names saved routes use (#2747).
 */

#include <juce_core/juce_core.h>

#include <map>

namespace magda {

/**
 * @brief The route name of each channel in @p open, from the interface's @p channelNames.
 *
 * Projects store these names, so every engine has to produce Tracktion's: outputs in pairs
 * (1+2, 3+4), inputs mono, and a two-channel interface named "Output 1" and "Output 2" (or
 * "Input"). MAGDA never changes Tracktion's pairing, so this is the only layout there is.
 */
std::map<int, juce::String> routeNamesByChannel(const juce::StringArray& channelNames,
                                                const juce::BigInteger& open, bool inputs);

}  // namespace magda
