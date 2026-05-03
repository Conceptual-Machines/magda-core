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
// ResolveResult
// ============================================================================

/**
 * @brief The result of resolving any Target form to a concrete ControlTarget.
 *
 * Thin wrapper composing a ControlTarget with provenance metadata. The
 * resolver populates `target` and `sourceLabel`; `ok()` is shorthand for
 * "resolution succeeded AND the target is valid."
 */
struct ResolveResult {
    ControlTarget target;
    juce::String sourceLabel;  // Human-readable provenance (for error messages)
    bool resolved = false;

    bool ok() const {
        return resolved && target.isValid();
    }

    /** Convenience factory for failure results. */
    static ResolveResult failure(const juce::String& reason) {
        ResolveResult r;
        r.sourceLabel = reason;
        r.resolved = false;
        return r;
    }
};

// ============================================================================
// TargetResolver
// ============================================================================

/**
 * @brief Resolves any Target variant or ParsedSigil to a concrete ResolveResult.
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
     * @brief Resolve a Target (variant) to a concrete ResolveResult.
     */
    ResolveResult resolve(const Target& target) const;

    /**
     * @brief Resolve a parsed '@'-sigil token.
     *
     * Only '@'-sigil ParsedSigils are accepted. '#' and '$' tokens are
     * rejected at the parser level and will never reach this function.
     */
    ResolveResult resolveSigil(const ParsedSigil& sigil) const;

  private:
    // ---- @ sigil implementation ----
    ResolveResult resolveAt(const ParsedSigil& sigil) const;

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
