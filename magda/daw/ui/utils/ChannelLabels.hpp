#pragma once

#include <juce_core/juce_core.h>

#include <initializer_list>

namespace magda::ChannelLabels {

/** @brief The driver's name verbatim, or its one-based index as a fallback. */
inline juce::String nameOrIndex(const juce::StringArray& names, int index) {
    if (juce::isPositiveAndBelow(index, names.size())) {
        const auto name = names[index].trim();
        if (name.isNotEmpty())
            return name;
    }
    return juce::String(index + 1);
}

/// The two driver names unchanged, or their indices where names are absent.
inline juce::String pair(const juce::StringArray& names, int first, int second) {
    const auto hasName = [&names](int index) {
        return juce::isPositiveAndBelow(index, names.size()) && names[index].trim().isNotEmpty();
    };
    const auto hasFirst = hasName(first);
    const auto hasSecond = hasName(second);
    if (!hasFirst && !hasSecond)
        return juce::String(first + 1) + "-" + juce::String(second + 1);
    return nameOrIndex(names, first) + " + " + nameOrIndex(names, second);
}

/// The driver's name unchanged, marked mono; the index is only a fallback.
inline juce::String mono(const juce::StringArray& names, int index) {
    return nameOrIndex(names, index) + " (mono)";
}

}  // namespace magda::ChannelLabels
