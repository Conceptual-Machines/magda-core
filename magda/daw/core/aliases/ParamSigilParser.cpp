#include "ParamSigilParser.hpp"

namespace magda {

// ============================================================================
// isSigilToken
// ============================================================================

bool isSigilToken(const juce::String& token) {
    if (token.isEmpty())
        return false;

    auto first = token[0];
    if (first != '@' && first != '#' && first != '$')
        return false;

    return token.containsChar('.');
}

// ============================================================================
// tryParse
// ============================================================================

std::optional<ParsedSigil> tryParse(const juce::String& token) {
    if (token.isEmpty())
        return std::nullopt;

    // Determine sigil
    Sigil sigil;
    switch (token[0]) {
        case '@':
            sigil = Sigil::At;
            break;
        case '#':
            sigil = Sigil::Hash;
            break;
        case '$':
            sigil = Sigil::Dollar;
            break;
        default:
            return std::nullopt;
    }

    // Strip the sigil character
    auto body = token.substring(1);

    // Split on the first '.'
    int dotPos = body.indexOfChar('.');
    if (dotPos <= 0)
        return std::nullopt;  // no dot, or dot at position 0 (empty pluginKey)

    auto rawPluginKey = body.substring(0, dotPos);
    auto paramKey = body.substring(dotPos + 1);

    if (rawPluginKey.isEmpty() || paramKey.isEmpty())
        return std::nullopt;

    ParsedSigil result;
    result.sigil = sigil;
    result.paramKey = paramKey;

    // For '#', split off trailing numeric instance suffix: pluginKey_N
    if (sigil == Sigil::Hash) {
        // Find the last underscore
        int lastUnderscore = rawPluginKey.lastIndexOfChar('_');
        if (lastUnderscore > 0) {
            auto suffix = rawPluginKey.substring(lastUnderscore + 1);
            // Check if suffix is a positive integer
            if (suffix.isNotEmpty() && suffix.containsOnly("0123456789") && suffix != "0") {
                int N = suffix.getIntValue();
                if (N >= 1) {
                    result.pluginKey = rawPluginKey.substring(0, lastUnderscore);
                    result.instanceIndex = N - 1;  // Convert to 0-based
                } else {
                    // Suffix is 0 -- invalid (instances numbered from 1)
                    return std::nullopt;
                }
            } else if (suffix.isNotEmpty() && suffix.containsOnly("0123456789")) {
                // suffix == "0" is invalid
                if (suffix == "0")
                    return std::nullopt;
                result.pluginKey = rawPluginKey;
                result.instanceIndex = 0;  // no valid suffix -> first instance
            } else {
                // Underscore is part of the plugin name, no numeric suffix
                result.pluginKey = rawPluginKey;
                result.instanceIndex = 0;  // default: first instance
            }
        } else {
            // No underscore in plugin key at all
            result.pluginKey = rawPluginKey;
            result.instanceIndex = 0;  // default: first instance
        }
    } else {
        result.pluginKey = rawPluginKey;
        result.instanceIndex = -1;  // not applicable
    }

    // Check for scoped forms
    if (sigil == Sigil::At || sigil == Sigil::Dollar) {
        static const juce::StringArray scopedKeys{"focused", "selected", "master"};
        result.isScoped = scopedKeys.contains(result.pluginKey);
    }

    return result;
}

}  // namespace magda
