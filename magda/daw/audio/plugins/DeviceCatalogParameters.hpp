#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>

#include "core/DeviceInfo.hpp"

namespace magda::daw::audio {

class MagdaDevice;

/**
 * @brief A registered MAGDA device, built off the catalog to be questioned and
 *        dropped.
 *
 * Neither engine is asked, so both get the same answer and no Edit is needed to
 * hold a plugin in - which the native engine does not have (#2601). @p
 * savedState is restored into it when given, for the devices whose parameter
 * set depends on it (the runtime Faust device reads its slots out of its dsp
 * source). Null for an id no catalog holds and for a device that is still a
 * host plugin rather than a MagdaDevice.
 */
std::unique_ptr<MagdaDevice> createDetachedDevice(const juce::String& pluginId,
                                                  const juce::String& savedState = {});

/// A device's saved state as a tree, in whichever format the project holds it:
/// most internal devices in a project folder are still saved as the engine's v1
/// XML, which `decode()` refuses by design (#2602). Invalid for state neither
/// reader accepts.
juce::ValueTree deviceStateTree(const juce::String& savedState);

/**
 * @brief Give the model every parameter a MAGDA device declares (#2613).
 *
 * `DeviceInfo::parameters` is the sole authority for an internal device's
 * parameters (#2317), and only the fork ever filled it: the array was read off
 * a plugin living in the fork's Edit. A device added under the native engine
 * had none, so every write to one was dropped for want of an entry to land on.
 *
 * Appends the declared parameters the model is missing, each at its own
 * paramIndex and default value. Entries the model already carries are left
 * alone, values included. Returns true when the device was changed.
 */
bool seedDeclaredParameters(magda::DeviceInfo& device);

}  // namespace magda::daw::audio
