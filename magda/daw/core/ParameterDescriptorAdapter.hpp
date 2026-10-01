#pragma once

#include <magda/sdk/device/ParameterDescriptor.hpp>

#include "ParameterInfo.hpp"

namespace magda {

/**
 * @brief The model's ParameterInfo for a device's described parameter.
 *
 * Carries every field the model keeps. Runtime and host-only fields (the plugin-native range,
 * the display text provider and table) keep their defaults, and currentValue starts at the
 * default value. step, automatable and readOnly have no model counterpart yet.
 */
ParameterInfo toParameterInfo(const sdk::ParameterDescriptor& descriptor);

/// A choice list's labels, in order.
std::vector<juce::String> choiceLabels(const std::vector<sdk::ParameterChoice>& choices);

/// UTF-8 text as a juce::String.
juce::String toJuceString(const std::string& text);

}  // namespace magda
