#pragma once

#include <juce_core/juce_core.h>

#include <map>

#include "../DeviceInfo.hpp"
#include "../SelectionManager.hpp"
#include "AliasRegistry.hpp"

namespace magda {

// ============================================================================
// AutoAliasGenerator
// ============================================================================

/**
 * @brief Generates and refreshes auto-generated @plugin.param aliases.
 *
 * Auto-generated aliases live in AliasLayer::AutoGen and are rebuilt every
 * time a plugin's parameter list is updated (e.g. after async load completes).
 * They give the LLM and DSL a stable, human-readable handle on any loaded
 * plugin without requiring any user configuration.
 *
 * Key: "{pluginKey}.{paramKey}"  (e.g. "serum_2.filter_1_cutoff")
 * Where pluginKey = normalizeParamName(deviceInfo.name)
 * And   paramKey  = uniquified normalizeParamName(param.name)
 */
class AutoAliasGenerator {
  public:
    /**
     * @brief Pure function: compute all alias entries for one device.
     *
     * Does NOT access any global state. Safe to call from tests without a
     * live DAW engine. The devicePath is stored in each StoredAlias so that
     * lookup() can materialise a concrete StaticTarget.
     *
     * @param deviceInfo  The device whose parameters should be aliased.
     * @param devicePath  The concrete path to the device in the chain tree.
     * @return Map of canonical alias name -> StoredAlias.
     */
    static std::map<juce::String, StoredAlias> computeForDevice(const DeviceInfo& deviceInfo,
                                                                const ChainNodePath& devicePath);

    /**
     * @brief Regenerate auto-gen aliases for a specific device and merge into
     *        AliasRegistry's AutoGen layer.
     *
     * Looks up the DeviceInfo (via its parameters already stored in
     * TrackManager) and the ChainNodePath, then calls computeForDevice() and
     * forwards the result to AliasRegistry::replaceAutoForDevice().
     *
     * Only touches AliasLayer::AutoGen - never touches other layers.
     *
     * @param deviceId  The MAGDA device to regenerate aliases for.
     */
    static void regenerateForDevice(DeviceId deviceId);
};

}  // namespace magda
