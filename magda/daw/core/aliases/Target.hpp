#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <variant>

#include "../ControlTarget.hpp"
#include "../SelectionManager.hpp"
#include "../TypeIds.hpp"

namespace magda {

// ============================================================================
// Target types
// ============================================================================

/**
 * @brief A reference to a named alias stored in AliasRegistry.
 *
 * The name is canonical (snake_case, normalized). The pluginType is used as a
 * disambiguation hint when the registry has entries for multiple plugin types
 * with the same alias name.
 */
struct AliasRef {
    juce::String name;
    juce::String pluginType;  // e.g. "serum", "surge_xt" -- may be empty

    bool operator==(const AliasRef& other) const {
        return name == other.name && pluginType == other.pluginType;
    }

    bool operator!=(const AliasRef& other) const {
        return !(*this == other);
    }
};

/**
 * @brief A reference to a named resolver in ResolverRegistry.
 *
 * The kind identifies the built-in resolver (e.g. "focused.macro").
 * The args are key=value pairs passed to the resolver.
 */
struct ResolverRef {
    juce::String kind;
    juce::StringPairArray args;

    bool operator==(const ResolverRef& other) const {
        return kind == other.kind && args == other.args;
    }

    bool operator!=(const ResolverRef& other) const {
        return !(*this == other);
    }
};

/**
 * @brief A Target is one of: ControlTarget, AliasRef, or ResolverRef.
 *
 * Use std::visit to dispatch on the active variant.
 */
using Target = std::variant<ControlTarget, AliasRef, ResolverRef>;

// ============================================================================
// Debug helpers
// ============================================================================

juce::String toDebugString(const Target& target);

// ============================================================================
// JSON round-trip
// ============================================================================

/**
 * Format:
 *   ControlTarget: {"kind":"static","controlKind":"...","path":{...},...}
 *   AliasRef:      {"kind":"alias","name":"...","pluginType":"..."}
 *   ResolverRef:   {"kind":"resolver","resolverKind":"...","args":{...}}
 */
juce::String encodeTarget(const Target& target);

/**
 * Returns nullopt when the string is malformed or the "kind" is unknown.
 */
std::optional<Target> decodeTarget(const juce::String& json);

}  // namespace magda
