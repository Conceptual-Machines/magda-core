#include "ParamSigilParser.hpp"

namespace magda {

// ============================================================================
// isSigilToken
// ============================================================================

bool isSigilToken(const juce::String& token) {
    if (token.isEmpty())
        return false;

    if (token[0] != '@')
        return false;

    return token.containsChar('.');
}

// ============================================================================
// tryParse
// ============================================================================

std::optional<ParsedSigil> tryParse(const juce::String& token) {
    if (token.isEmpty())
        return std::nullopt;

    // Only '@' is accepted; '#' and '$' are rejected as malformed.
    if (token[0] != '@')
        return std::nullopt;

    // Strip the '@' character
    auto body = token.substring(1);

    // Split on the first '.'
    int dotPos = body.indexOfChar('.');
    if (dotPos <= 0)
        return std::nullopt;  // no dot, or dot at position 0 (empty pluginKey)

    auto pluginKey = body.substring(0, dotPos);
    auto paramKey = body.substring(dotPos + 1);

    if (pluginKey.isEmpty() || paramKey.isEmpty())
        return std::nullopt;

    ParsedSigil result;
    result.pluginKey = pluginKey;
    result.paramKey = paramKey;

    // Check for scoped forms
    static const juce::StringArray scopedKeys{"focused", "selected", "master"};
    result.isScoped = scopedKeys.contains(result.pluginKey);

    return result;
}

}  // namespace magda
