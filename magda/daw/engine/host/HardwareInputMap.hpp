#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

/**
 * @file HardwareInputMap.hpp
 * @brief Which callback input channels a track's saved audio input reads (#2553).
 */

namespace magda::daw::engine_host {

/// Input names mapped to callback channel indices.
using HardwareInputMap = std::map<juce::String, std::vector<int>>;

/**
 * @brief Every name the input menus can store, resolved against the open device.
 *
 * A channel's wave-in name, "stereo:" plus a pair's first channel's name, or the
 * "In N" fallback (RoutingSyncHelper::populateAudioInputOptions). A wave-in
 * naming two channels reads both under its bare name. An index is a channel's
 * position among @p active, as the callback packs them; an empty @p enabled
 * means every active channel, as the menus read it.
 */
HardwareInputMap resolveHardwareInputs(const juce::BigInteger& enabled,
                                       const std::map<int, juce::String>& namesByChannel,
                                       const juce::BigInteger& active);

}  // namespace magda::daw::engine_host
