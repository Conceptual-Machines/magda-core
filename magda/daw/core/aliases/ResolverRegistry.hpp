#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <vector>

#include "ChainContext.hpp"
#include "Target.hpp"

namespace magda {

// ============================================================================
// AliasResolver interface
// ============================================================================

/**
 * @brief Interface for a built-in resolver that materialises a ResolverRef.
 *
 * Built-in resolvers are registered in ResolverRegistry on construction.
 * The kind() string must match the ResolverRef::kind field exactly.
 */
class AliasResolver {
  public:
    virtual ~AliasResolver() = default;

    /**
     * @brief Return the resolver kind identifier.
     *
     * Must be a unique, stable ASCII string (no unicode or whitespace).
     */
    virtual juce::String kind() const = 0;

    /**
     * @brief Attempt to materialise a ControlTarget.
     *
     * @param args  Key-value arguments from the ResolverRef.
     * @param ctx   Chain context for querying the live DAW state.
     * @return      Populated ControlTarget on success, nullopt on failure.
     */
    virtual std::optional<ControlTarget> resolve(const juce::StringPairArray& args,
                                                 const ChainContext& ctx) const = 0;
};

// ============================================================================
// Built-in resolvers
// ============================================================================

/**
 * @brief Resolve the Nth macro of the currently focused device.
 *
 * args: { "macroIndex": "N" }  (0-based, default 0)
 */
class FocusedDeviceMacroResolver : public AliasResolver {
  public:
    juce::String kind() const override {
        return "focused.macro";
    }
    std::optional<ControlTarget> resolve(const juce::StringPairArray& args,
                                         const ChainContext& ctx) const override;
};

/**
 * @brief Resolve the volume parameter of the selected track's first device.
 *
 * kind: "selected.volume"
 */
class SelectedTrackVolumeResolver : public AliasResolver {
  public:
    juce::String kind() const override {
        return "selected.volume";
    }
    std::optional<ControlTarget> resolve(const juce::StringPairArray& args,
                                         const ChainContext& ctx) const override;
};

/**
 * @brief Resolve the pan parameter of the selected track's first device.
 *
 * kind: "selected.pan"
 */
class SelectedTrackPanResolver : public AliasResolver {
  public:
    juce::String kind() const override {
        return "selected.pan";
    }
    std::optional<ControlTarget> resolve(const juce::StringPairArray& args,
                                         const ChainContext& ctx) const override;
};

/**
 * @brief Resolve the volume parameter on the master track.
 *
 * kind: "master.volume"
 */
class MasterVolumeResolver : public AliasResolver {
  public:
    juce::String kind() const override {
        return "master.volume";
    }
    std::optional<ControlTarget> resolve(const juce::StringPairArray& args,
                                         const ChainContext& ctx) const override;
};

/**
 * @brief Resolve the pan parameter on the master track.
 *
 * kind: "master.pan"
 */
class MasterPanResolver : public AliasResolver {
  public:
    juce::String kind() const override {
        return "master.pan";
    }
    std::optional<ControlTarget> resolve(const juce::StringPairArray& args,
                                         const ChainContext& ctx) const override;
};

// ============================================================================
// ResolverRegistry
// ============================================================================

/**
 * @brief Singleton registry of AliasResolver instances.
 *
 * Built-ins are registered in the constructor. Plugins or tests may add
 * custom resolvers via registerResolver().
 */
class ResolverRegistry {
  public:
    static ResolverRegistry& getInstance();

    /**
     * @brief Find a resolver by kind string.
     *
     * Returns nullptr if no resolver is registered for that kind.
     */
    const AliasResolver* findResolver(const juce::String& kind) const;

    /**
     * @brief Register a custom resolver.
     *
     * If a resolver with the same kind is already registered, the new one
     * replaces it.
     */
    void registerResolver(std::unique_ptr<AliasResolver> resolver);

  private:
    ResolverRegistry();

    std::vector<std::unique_ptr<AliasResolver>> resolvers_;
};

}  // namespace magda
