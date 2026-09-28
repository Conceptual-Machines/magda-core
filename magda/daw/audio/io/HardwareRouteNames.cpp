#include "HardwareRouteNames.hpp"

namespace magda {

namespace {

/** @brief Tracktion's mergeTwoNames: "Out 1" and "Out 2" read "Out 1 + 2". */
juce::String mergeTwoNames(const juce::String& first, const juce::String& second) {
    const auto bracketed = [](const juce::String& name) {
        return name.fromLastOccurrenceOf("(", false, false)
            .upToFirstOccurrenceOf(")", false, false)
            .trim();
    };
    const auto stem = [](const juce::String& name) {
        return name.upToLastOccurrenceOf("(", false, false).trim();
    };
    if (bracketed(first).isNotEmpty() && bracketed(second).isNotEmpty() &&
        first.endsWithChar(')') && second.endsWithChar(')') && stem(first) == stem(second))
        return stem(first) + " (" + bracketed(first) + " + " + bracketed(second) + ")";

    const auto trailingDigits = [](const juce::String& name) {
        auto start = name.length();
        while (start > 0 && juce::CharacterFunctions::isDigit(name[start - 1]))
            --start;
        return name.substring(start);
    };
    const auto firstNumber = trailingDigits(first);
    const auto secondNumber = trailingDigits(second);
    const auto firstStem = first.dropLastCharacters(firstNumber.length());
    if (firstNumber.isNotEmpty() && secondNumber.isNotEmpty() &&
        firstStem == second.dropLastCharacters(secondNumber.length()))
        return firstStem + firstNumber + " + " + secondNumber;

    return first + " + " + second;
}

}  // namespace

std::map<int, juce::String> routeNamesByChannel(const juce::StringArray& channelNames,
                                                const juce::BigInteger& open, bool inputs) {
    auto names = channelNames;
    if (names.size() == 2) {
        const juce::String prefix = inputs ? "Input " : "Output ";
        names.set(0, prefix + "1");
        names.set(1, prefix + "2");
    }

    std::map<int, juce::String> byChannel;
    const auto name = [&](int channel, const juce::String& deviceName) {
        if (open[channel])
            byChannel[channel] = deviceName;
    };

    for (auto channel = 0; channel < names.size(); ++channel) {
        if (!inputs && channel + 1 < names.size()) {
            const auto pair = mergeTwoNames(names[channel], names[channel + 1]);
            name(channel, pair);
            name(++channel, pair);
        } else {
            name(channel, names[channel]);
        }
    }
    return byChannel;
}

}  // namespace magda
