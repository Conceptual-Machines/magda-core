#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace magda {

// ============================================================================
// Parsed sigil
// ============================================================================

/**
 * @brief Result of parsing a single '@'-sigil token such as "@serum.filter_1".
 *
 * Fields:
 *   pluginKey  -- the plugin type/alias before the dot, e.g. "serum"
 *   paramKey   -- the parameter key after the dot, e.g. "filter_1"
 *   isScoped   -- true when pluginKey is a special context scope:
 *                 "focused", "selected", "master"
 *
 * Scoped forms (isScoped == true):
 *   @focused.macro_1   -- first macro of the currently focused device
 *   @selected.volume   -- volume of the selected track
 *   @master.pan        -- pan of the master track
 */
struct ParsedSigil {
    juce::String pluginKey;  // e.g. "serum", "focused", "master"
    juce::String paramKey;   // e.g. "filter_1", "volume"
    bool isScoped = false;   // true for focused/selected/master
};

// ============================================================================
// Parser API
// ============================================================================

/**
 * @brief Attempt to parse an '@'-sigil token.
 *
 * Accepts tokens of the form:
 *   @pluginKey.paramKey
 *   @scopeKey.paramKey   (isScoped=true when scopeKey in {focused, selected, master})
 *
 * Returns nullopt if:
 *   - The string does not start with '@'.
 *   - The string starts with '#' or '$' (malformed — rejected).
 *   - There is no '.' separating pluginKey from paramKey.
 *   - pluginKey or paramKey is empty after splitting.
 */
std::optional<ParsedSigil> tryParse(const juce::String& token);

/**
 * @brief Quick check: does this string look like an '@'-sigil token?
 *
 * Returns true if the string starts with '@' and contains a '.'.
 * Does NOT validate the full grammar (use tryParse for that).
 * Returns false for '#' or '$' prefixes.
 */
bool isSigilToken(const juce::String& token);

}  // namespace magda
