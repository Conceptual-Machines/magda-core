#include "midi/MidiDeviceMatch.hpp"

namespace magda::midi {

bool matches(const juce::String& storedKey, const juce::String& liveIdentifier,
             const juce::String& liveName) {
    if (storedKey.isEmpty())
        return false;
    if (storedKey == liveIdentifier)
        return true;
    if (liveName.isNotEmpty() && storedKey.equalsIgnoreCase(liveName))
        return true;
    return false;
}

bool matchedByNameOnly(const juce::String& storedKey, const juce::String& liveIdentifier,
                       const juce::String& liveName) {
    if (storedKey.isEmpty())
        return false;
    if (storedKey == liveIdentifier)
        return false;  // identifier match wins; not name-only
    if (liveName.isNotEmpty() && storedKey.equalsIgnoreCase(liveName))
        return true;
    return false;
}

std::optional<juce::MidiDeviceInfo> resolve(const juce::Array<juce::MidiDeviceInfo>& devices,
                                            const juce::String& storedKey) {
    if (storedKey.isEmpty())
        return std::nullopt;

    // Pass 1: identifier match (preferred — exact, stable on this machine).
    for (const auto& d : devices)
        if (storedKey == d.identifier)
            return d;

    // Pass 2: display name match (case-insensitive fallback).
    for (const auto& d : devices)
        if (d.name.isNotEmpty() && storedKey.equalsIgnoreCase(d.name))
            return d;

    return std::nullopt;
}

bool sameMidiHardware(const juce::String& a, const juce::String& b) {
    if (a.isEmpty() || b.isEmpty())
        return false;
    if (a.equalsIgnoreCase(b))
        return true;

    auto tokensWithoutDirection = [](const juce::String& name) {
        auto tokens = juce::StringArray::fromTokens(name, " ", {});
        tokens.removeEmptyStrings();
        for (int i = tokens.size(); --i >= 0;) {
            const auto t = tokens[i].toLowerCase();
            if (t == "in" || t == "out" || t == "input" || t == "output")
                tokens.remove(i);
        }
        return tokens;
    };
    const auto tokensA = tokensWithoutDirection(a);
    const auto tokensB = tokensWithoutDirection(b);
    const auto stemA = tokensA.joinIntoString(" ").toLowerCase();
    const auto stemB = tokensB.joinIntoString(" ").toLowerCase();
    if (stemA.isNotEmpty() && stemA == stemB)
        return true;

    // Tier 3 requires the first TWO tokens to agree: the first word alone is
    // typically the brand, shared by unrelated devices.
    if (!a.containsAnyOf("0123456789") && !b.containsAnyOf("0123456789")) {
        if (tokensA.size() >= 2 && tokensB.size() >= 2 && tokensA[0].length() >= 3 &&
            tokensA[0].equalsIgnoreCase(tokensB[0]) && tokensA[1].equalsIgnoreCase(tokensB[1]))
            return true;
    }
    return false;
}

}  // namespace magda::midi
