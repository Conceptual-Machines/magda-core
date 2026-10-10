#pragma once

#include <juce_core/juce_core.h>

#include <initializer_list>

#include "../../core/ChannelNames.hpp"

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
inline juce::String driverPair(const juce::StringArray& names, int first, int second) {
    const auto hasName = [&names](int index) {
        return juce::isPositiveAndBelow(index, names.size()) && names[index].trim().isNotEmpty();
    };
    const auto hasFirst = hasName(first);
    const auto hasSecond = hasName(second);
    if (!hasFirst && !hasSecond)
        return juce::String(first + 1) + "-" + juce::String(second + 1);
    return nameOrIndex(names, first) + " + " + nameOrIndex(names, second);
}

/**
 * @brief The user's name for the pair with the driver's label after it, else the driver's.
 *
 * Only an adjacent pair carries a user name; that is the only kind Audio Settings names.
 */
inline juce::String pair(const juce::StringArray& names, int first, int second,
                         const ChannelNames& user = {}) {
    const auto driver = driverPair(names, first, second);
    const auto named = second == first + 1 ? user.nameOf(first, true) : std::string();
    if (named.empty())
        return driver;
    return juce::String::fromUTF8(named.c_str()) + " (" + driver + ")";
}

/// The user's name or the driver's, marked mono; the index is only a fallback.
inline juce::String mono(const juce::StringArray& names, int index, const ChannelNames& user = {}) {
    const auto named = user.nameOf(index, false);
    if (named.empty())
        return nameOrIndex(names, index) + " (mono)";
    return juce::String::fromUTF8(named.c_str()) + " (" + nameOrIndex(names, index) + ", mono)";
}

}  // namespace magda::ChannelLabels
