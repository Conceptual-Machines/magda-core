#pragma once

#include <juce_core/juce_core.h>

#include <initializer_list>

namespace magda::ChannelLabels {

/**
 * @brief The interface's own name for channel @p index, or empty where it only
 *        numbered it (#2734).
 *
 * A number alone does not say that a MOTU M4's inputs 5 to 8 are its loopback
 * channels. "In 3" on channel 3, or JUCE's own "Input 3" fallback, adds
 * nothing beside the number, so those are left out.
 */
inline juce::String nameOf(const juce::StringArray& names, int index) {
    if (!juce::isPositiveAndBelow(index, names.size()))
        return {};

    auto name = names[index].trim();
    const auto number = juce::String(index + 1);
    for (const auto* generic : {"In ", "Input ", "Out ", "Output "})
        if (name == juce::String(generic) + number)
            return {};
    return name;
}

/// "5-6 Loopback 1+2", or "5-6" where neither channel is named.
inline juce::String pair(const juce::StringArray& names, int first, int second) {
    auto label = juce::String(first + 1) + "-" + juce::String(second + 1);
    if (nameOf(names, first).isEmpty() && nameOf(names, second).isEmpty())
        return label;

    // Once either is named, both are, so a half-named pair reads "In 1 + Talkback".
    const auto spelled = [&names](int index) {
        const auto name =
            juce::isPositiveAndBelow(index, names.size()) ? names[index].trim() : juce::String();
        return name.isNotEmpty() ? name : juce::String(index + 1);
    };
    const auto a = spelled(first);
    const auto b = spelled(second);

    // "Loopback 1" and "Loopback 2" share everything but the number: "Loopback 1+2".
    const auto stemA = a.upToLastOccurrenceOf(" ", false, false);
    const auto numberA = a.fromLastOccurrenceOf(" ", false, false);
    const auto numberB = b.fromLastOccurrenceOf(" ", false, false);
    if (stemA.isNotEmpty() && stemA == b.upToLastOccurrenceOf(" ", false, false) &&
        numberA.containsOnly("0123456789") && numberB.containsOnly("0123456789"))
        return label + " " + stemA + " " + numberA + "+" + numberB;

    return label + " " + a + " + " + b;
}

/// "5 Loopback 1 (mono)", or "5 (mono)" where the channel is not named.
inline juce::String mono(const juce::StringArray& names, int index) {
    auto label = juce::String(index + 1);
    if (const auto name = nameOf(names, index); name.isNotEmpty())
        label += " " + name;
    return label + " (mono)";
}

}  // namespace magda::ChannelLabels
