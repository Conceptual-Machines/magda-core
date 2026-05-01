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

}  // namespace magda::midi
