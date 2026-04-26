#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "AliasRegistry.hpp"
#include "ChainContext.hpp"
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
    StaticTarget::Owner owner = StaticTarget::Owner::PluginParam;

    // Populated only when owner == ModParam.
    ModId modId = INVALID_MOD_ID;
    int modParamIndex = -1;

    juce::String sourceLabel;  // Human-readable provenance (for error messages)
    bool resolved = false;

    bool ok() const {
        if (!resolved || !devicePath.isValid())
            return false;
        if (owner == StaticTarget::Owner::ModParam)
            return modId != INVALID_MOD_ID && modParamIndex >= 0;
        return paramIndex >= 0;
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
 *
 * Resolution order for '@name.param':
 *   1. If ChainContext::selectedTrack() is valid, scan that track's chain
 *      devices for a device whose normalised name matches 'name'.  If found,
 *      resolve to that device's concrete paramKey.
 *   2. Otherwise look up AliasRegistry walking layers
 *      (UserProject > UserGlobal > Curated > AutoGen).
 *   3. Scoped forms (@focused.*, @selected.*, @master.*) route through
 *      ResolverRegistry.
 */
class TargetResolver {
  public:
    TargetResolver(AliasRegistry& aliasRegistry, ResolverRegistry& resolverRegistry,
                   ChainContext& chainContext)
        : aliasRegistry_(aliasRegistry),
          resolverRegistry_(resolverRegistry),
          chainContext_(chainContext) {}

    // ========================================================================
    // Primary API
    // ========================================================================

    /**
     * @brief Resolve a Target (variant) to a concrete ResolvedTarget.
     */
    ResolvedTarget resolve(const Target& target) const;

    /**
     * @brief Resolve a parsed '@'-sigil token.
     *
     * Only '@'-sigil ParsedSigils are accepted. '#' and '$' tokens are
     * rejected at the parser level and will never reach this function.
     */
    ResolvedTarget resolveSigil(const ParsedSigil& sigil) const;

  private:
    // ---- @ sigil implementation ----
    ResolvedTarget resolveAt(const ParsedSigil& sigil) const;

    // ---- helpers ----

    // Given a list of devices, find the first device whose normalised plugin
    // name or user device name matches pluginKey. Returns nullptr if not found.
    static const ChainContext::DeviceWithPath* findFirstMatchingDevice(
        const std::vector<ChainContext::DeviceWithPath>& devices, const juce::String& pluginKey);

    // Given a device, find a param by name (case-insensitive normalised key).
    static int findParamByKey(const DeviceInfo& device, const juce::String& paramKey);

    AliasRegistry& aliasRegistry_;
    ResolverRegistry& resolverRegistry_;
    ChainContext& chainContext_;
};

}  // namespace magda
