#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "DarkTheme.hpp"

// Stable string keys and colour parsing for user-authored JSON themes (#88).
//
// Every ColourRole / SyntaxColourRole has a canonical camelCase name that
// mirrors its enum identifier (TEXT_PRIMARY -> "textPrimary"). Lookups are
// deliberately forgiving: case and separators (_ - space) are ignored, so
// "TEXT_PRIMARY", "text-primary" and "textPrimary" all resolve to the same
// role. This keeps hand-edited files robust without a brittle exact-match
// contract.

namespace magda {

/** Canonical camelCase name for a colour role (never null for valid roles). */
const char* colourRoleName(ColourRole role);

/** Canonical camelCase name for a syntax colour role. */
const char* syntaxColourRoleName(SyntaxColourRole role);

/** Resolves a JSON key to a colour role, ignoring case and separators. */
std::optional<ColourRole> findColourRole(const juce::String& key);

/** Resolves a JSON key to a syntax colour role, ignoring case and separators. */
std::optional<SyntaxColourRole> findSyntaxColourRole(const juce::String& key);

/** Parses "#RRGGBB" or "#AARRGGBB" (leading # or 0x optional). A 6-digit
 *  value is treated as fully opaque. Returns nullopt for anything that is not
 *  a valid 6- or 8-digit hex colour. */
std::optional<juce::Colour> parseColourString(const juce::String& text);

/** Formats a colour as "#RRGGBB" when fully opaque, else "#AARRGGBB". */
juce::String colourToHexString(juce::Colour colour);

}  // namespace magda
