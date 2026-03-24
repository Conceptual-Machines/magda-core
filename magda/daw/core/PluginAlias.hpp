#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Generate a snake_case alias from a plugin name.
 *
 * e.g. "Pro-Q 3" → "pro_q_3", "Surge XT" → "surge_xt"
 *
 * Single source of truth — used by both the plugin browser UI and the DSL
 * interpreter so aliases always match.
 */
inline juce::String pluginNameToAlias(const juce::String& pluginName) {
    juce::String result;

    for (int i = 0; i < pluginName.length(); ++i) {
        auto ch = pluginName[i];

        // Skip non-alphanumeric characters
        if (!juce::CharacterFunctions::isLetterOrDigit(ch))
            continue;

        // Insert underscore before uppercase letter if preceded by lowercase or digit
        if (result.isNotEmpty() && juce::CharacterFunctions::isUpperCase(ch)) {
            auto prev = pluginName[i - 1];
            if (juce::CharacterFunctions::isLowerCase(prev) ||
                juce::CharacterFunctions::isDigit(prev)) {
                result += '_';
            }
        }

        // Insert underscore between letter and digit transitions
        if (result.isNotEmpty() && juce::CharacterFunctions::isDigit(ch)) {
            auto lastChar = result[result.length() - 1];
            if (juce::CharacterFunctions::isLetter(lastChar) && lastChar != '_') {
                result += '_';
            }
        }

        result += juce::CharacterFunctions::toLowerCase(ch);
    }

    // Clean up doubled/leading/trailing underscores
    while (result.contains("__"))
        result = result.replace("__", "_");
    while (result.startsWith("_"))
        result = result.substring(1);
    while (result.endsWith("_"))
        result = result.dropLastCharacters(1);

    return result;
}

}  // namespace magda
