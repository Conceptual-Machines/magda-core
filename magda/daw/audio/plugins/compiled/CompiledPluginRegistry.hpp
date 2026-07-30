#pragma once

#include <memory>
#include <span>

#include "core/TypeIds.hpp"
#include "plugins/DevicePluginHandle.hpp"

namespace magda {
class DeviceProcessor;
}

namespace magda::daw::audio {
class MagdaDevice;
}

namespace magda::daw::audio::compiled {

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
 * accessor (e.g. `getMagdaDelaySpec()`); the host-owned aggregator
 * explicitly lists those accessors. Static self-registration was
 * deliberately avoided — explicit aggregation makes the link order
 * deterministic and trivial to unit-test.
 */
struct CompiledPluginSpec {
    const char* pluginId;         // matches `xmlTypeName`
    const char* displayName;      // user-facing name in browser / chain
    const char* browserCategory;  // "Modulation" / "Delay" / ...
    const char* description;      // tooltip / catalog blurb
    std::unique_ptr<MagdaDevice> (*createDevice)(const DevicePluginCreationContext&) = nullptr;
    // Transitional hook for compiled devices not yet migrated to MagdaDevice.
    DevicePluginPtr (*createPlugin)(const DevicePluginCreationContext&) = nullptr;
    const char* aliasKey = nullptr;  // defaults to pluginId when null
    const AliasSpec* aliases = nullptr;
    int aliasCount = 0;
    bool isInstrument = false;  // synth/MIDI instrument vs effect (browser + placement)
};

/// All compiled-plugin specs known to the current host, in stable iteration order.
/// Implemented by the host compatibility target while legacy compiled devices
/// remain Tracktion-native; the neutral base-device archive does not depend on it.
std::span<const CompiledPluginSpec* const> getAllCompiledPluginSpecs();

/// Returns null if `pluginId` doesn't match any compiled plugin id or load alias.
const CompiledPluginSpec* findCompiledPluginSpec(const juce::String& pluginId);

}  // namespace magda::daw::audio::compiled
