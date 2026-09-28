#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>

#include "core/DeviceInfo.hpp"
#include "plugins/DevicePluginDefaults.hpp"

namespace magda::daw::audio {

class MagdaDevice;

/**
 * @brief A registered MAGDA device built off the catalog, with no Edit behind it (#2601).
 *
 * @p savedState is restored into it when given, for devices whose parameter set
 * depends on it. Null for an unknown id or a device that is still a host plugin.
 *
 * @p defaults is what a device with nothing saved starts from -- the user's
 * last-used settings, which no session registry answers for a detached
 * creation (#2663).
 */
std::unique_ptr<MagdaDevice> createDetachedDevice(const juce::String& pluginId,
                                                  const juce::String& savedState = {},
                                                  const DevicePluginDefaults& defaults = {});

/// A device's saved state as a tree, in either format a project holds it in;
/// `decode()` refuses the engine's v1 XML by design (#2602). Invalid when unreadable.
juce::ValueTree deviceStateTree(const juce::String& savedState);

/**
 * @brief Copy what a registered device declares onto its model record.
 *
 * Appends the declared parameters `DeviceInfo::parameters` is missing (#2613),
 * each at its own paramIndex and default value, and sets
 * `DeviceInfo::forwardsMidiInput`. Existing parameters keep their values.
 * Returns true when the device was changed.
 */
bool applyDeviceDeclaration(magda::DeviceInfo& device);

}  // namespace magda::daw::audio
