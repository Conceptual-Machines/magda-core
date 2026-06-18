#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <span>

#include "core/TypeIds.hpp"

namespace magda {
class DeviceProcessor;
}

namespace magda::daw::audio::compiled {

namespace te = tracktion::engine;

struct AliasSpec {
    const char* alias;
    int paramIndex;
    const char* paramName;  // Used as paramNameAtSetTime for drift recovery.
};

/**
 * @brief Identity + factory of a single compiled-Faust plugin.
 *
 * The audio-side spec is data-only: no UI types, no juce::Component
 * references. Each compiled plugin exposes its spec via a named
 * accessor (e.g. `getMagdaDelaySpec()`); the aggregator below
 * explicitly lists those accessors. Static self-registration was
 * deliberately avoided — explicit aggregation makes the link order
 * deterministic and trivial to unit-test.
 */
struct CompiledPluginSpec {
    const char* pluginId;         // matches `xmlTypeName`
    const char* displayName;      // user-facing name in browser / chain
    const char* browserCategory;  // "Modulation" / "Delay" / ...
    const char* description;      // tooltip / catalog blurb
    te::Plugin::Ptr (*createPlugin)(const te::PluginCreationInfo& info);
    const char* aliasKey = nullptr;  // defaults to pluginId when null
    const AliasSpec* aliases = nullptr;
    int aliasCount = 0;
};

/// All compiled-plugin specs known to MAGDA, in stable iteration order.
std::span<const CompiledPluginSpec* const> getAllCompiledPluginSpecs();

/// Returns null if `pluginId` doesn't match any compiled plugin id or load alias.
const CompiledPluginSpec* findCompiledPluginSpec(const juce::String& pluginId);

/// Creates the runtime processor for a compiled plugin instance.
std::unique_ptr<magda::DeviceProcessor> createCompiledPluginProcessor(
    const CompiledPluginSpec& spec, DeviceId deviceId, te::Plugin::Ptr plugin);

}  // namespace magda::daw::audio::compiled
