#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "AliasRegistry.hpp"
#include "ChainContext.hpp"
#include "LocalBindings.hpp"
#include "ParamSigilParser.hpp"
#include "ResolverRegistry.hpp"
#include "Target.hpp"

namespace magda {

// ============================================================================
// ResolvedTarget
// ============================================================================

/**
 * @brief The result of resolving any Target form to a concrete location.
 */
struct ResolvedTarget {
    ChainNodePath devicePath;
    int paramIndex = -1;
    juce::String sourceLabel;  // Human-readable provenance (for error messages)
    bool resolved = false;

    bool ok() const {
        return resolved && devicePath.isValid() && paramIndex >= 0;
    }

    /** Convenience factory for failure results. */
    static ResolvedTarget failure(const juce::String& reason) {
        ResolvedTarget r;
        r.sourceLabel = reason;
        r.resolved = false;
        return r;
    }
};

// ============================================================================
// TargetResolver
// ============================================================================

/**
 * @brief Resolves any Target variant or ParsedSigil to a concrete ResolvedTarget.
 *
 * Holds non-owning references; callers are responsible for lifetime.
 * LocalBindings pointer may be nullptr ($ sigil will always fail gracefully).
 */
class TargetResolver {
  public:
    TargetResolver(AliasRegistry& aliasRegistry, ResolverRegistry& resolverRegistry,
                   ChainContext& chainContext, LocalBindings* localBindings = nullptr)
        : aliasRegistry_(aliasRegistry),
          resolverRegistry_(resolverRegistry),
          chainContext_(chainContext),
          localBindings_(localBindings) {}

    // ========================================================================
    // Primary API
    // ========================================================================

    /**
     * @brief Resolve a Target (variant) to a concrete ResolvedTarget.
     */
    ResolvedTarget resolve(const Target& target) const;

    /**
     * @brief Resolve a parsed sigil token.
     *
     * Dispatch rules:
     *   @  -> lookup in AliasRegistry; if path absent, scan focused chain
     *         for a device matching pluginTypeKey; apply drift fallback.
     *   #  -> scan devicesInFocusedChain() for the Nth matching device;
     *         return failure if no chain focused.
     *   $  -> look up in LocalBindings; return failure if not bound or
     *         LocalBindings is null.
     */
    ResolvedTarget resolveSigil(const ParsedSigil& sigil) const;

  private:
    // ---- @ sigil implementation ----
    ResolvedTarget resolveAt(const ParsedSigil& sigil) const;

    // ---- # sigil implementation ----
    ResolvedTarget resolveHash(const ParsedSigil& sigil) const;

    // ---- $ sigil implementation ----
    ResolvedTarget resolveDollar(const ParsedSigil& sigil) const;

    // ---- helpers ----

    // Given a list of devices, find the Nth device (0-based) whose normalised
    // plugin name or user device name contains pluginKey. Returns nullptr if not found.
    static const ChainContext::DeviceWithPath* findNthMatchingDevice(
        const std::vector<ChainContext::DeviceWithPath>& devices, const juce::String& pluginKey,
        int instanceIndex);

    // Given a device, find a param by name (case-insensitive normalised key).
    static int findParamByKey(const DeviceInfo& device, const juce::String& paramKey);

    AliasRegistry& aliasRegistry_;
    ResolverRegistry& resolverRegistry_;
    ChainContext& chainContext_;
    LocalBindings* localBindings_;
};

}  // namespace magda
