#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <variant>

#include "../SelectionManager.hpp"

namespace magda {

// ============================================================================
// Target types
// ============================================================================

/**
 * @brief A fully-resolved, concrete reference to a plugin parameter.
 *
 * Carries the exact path to the device and the parameter index within it.
 * This is the "materialized" form -- all alias resolution ends here.
 */
struct StaticTarget {
    ChainNodePath devicePath;
    int paramIndex = -1;

    bool isValid() const {
        return devicePath.isValid() && paramIndex >= 0;
    }

    bool operator==(const StaticTarget& other) const {
        return devicePath == other.devicePath && paramIndex == other.paramIndex;
    }

    bool operator!=(const StaticTarget& other) const {
        return !(*this == other);
    }
};

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
 * The kind identifies the built-in resolver (e.g. "focused_device_macro").
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
 * @brief A Target is one of: StaticTarget, AliasRef, or ResolverRef.
 *
 * Use std::visit to dispatch on the active variant.
 */
using Target = std::variant<StaticTarget, AliasRef, ResolverRef>;

// ============================================================================
// Debug helpers
// ============================================================================

/**
 * @brief Return a human-readable description of a Target (for logging/debugging).
 */
juce::String toDebugString(const Target& target);

// ============================================================================
// JSON round-trip
// ============================================================================

/**
 * @brief Encode a Target to a JSON string (for storage / transport).
 *
 * Format:
 *   StaticTarget:  {"kind":"static","path":{...},"paramIndex":N}
 *   AliasRef:      {"kind":"alias","name":"...","pluginType":"..."}
 *   ResolverRef:   {"kind":"resolver","resolverKind":"...","args":{...}}
 */
juce::String encodeTarget(const Target& target);

/**
 * @brief Decode a Target from a JSON string produced by encodeTarget().
 *
 * Returns nullopt when the string is malformed or the "kind" is unknown.
 */
std::optional<Target> decodeTarget(const juce::String& json);

}  // namespace magda
