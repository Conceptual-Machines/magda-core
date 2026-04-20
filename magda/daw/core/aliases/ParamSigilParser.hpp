#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace magda {

// ============================================================================
// Sigil enum
// ============================================================================

/**
 * @brief The three parameter reference sigils.
 *
 *   @  type-level persistent alias  (looks up AliasRegistry)
 *   #  instance-level chain-scoped  (scans focused chain at authoring time)
 *   $  plan-local DSL binding       (looks up LocalBindings)
 */
enum class Sigil { At, Hash, Dollar };

// ============================================================================
// Parsed sigil
// ============================================================================

/**
 * @brief Result of parsing a single sigil token such as "@serum.filter_1".
 *
 * Fields:
 *   sigil         -- which sigil character was used
 *   pluginKey     -- the plugin type/alias before the dot, e.g. "serum"
 *   instanceIndex -- for '#': the numeric instance suffix (0 = "first" / no suffix)
 *                    for '@' / '$': always -1
 *   paramKey      -- the parameter key after the dot, e.g. "filter_1"
 *   isScoped      -- true when pluginKey is a special context scope:
 *                    "focused", "selected", "master"
 *
 * Scoped forms (isScoped == true):
 *   @focused.macro_1   -- first macro of the currently focused device
 *   @selected.volume   -- volume of the selected track
 *   @master.pan        -- pan of the master track
 */
struct ParsedSigil {
    Sigil sigil = Sigil::At;
    juce::String pluginKey;  // e.g. "serum", "focused", "master"
    int instanceIndex = -1;  // -1 = not applicable or no suffix
    juce::String paramKey;   // e.g. "filter_1", "volume"
    bool isScoped = false;   // true for focused/selected/master
};

// ============================================================================
// Parser API
// ============================================================================

/**
 * @brief Attempt to parse a sigil token.
 *
 * Accepts tokens of the form:
 *   @pluginKey.paramKey
 *   @scopeKey.paramKey          (isScoped=true when scopeKey in {focused,selected,master})
 *   #pluginKey.paramKey         (instanceIndex=0 -- first/only instance)
 *   #pluginKey_N.paramKey       (instanceIndex=N-1, N>=1)
 *   $localName.paramKey
 *
 * Returns nullopt if:
 *   - The string does not start with '@', '#', or '$'.
 *   - There is no '.' separating pluginKey from paramKey.
 *   - pluginKey or paramKey is empty after splitting.
 *   - For '#', the instance suffix is present but not a positive integer.
 */
std::optional<ParsedSigil> tryParse(const juce::String& token);

/**
 * @brief Quick check: does this string look like a sigil token?
 *
 * Returns true if the string starts with '@', '#', or '$' and contains a '.'.
 * Does NOT validate the full grammar (use tryParse for that).
 */
bool isSigilToken(const juce::String& token);

}  // namespace magda
