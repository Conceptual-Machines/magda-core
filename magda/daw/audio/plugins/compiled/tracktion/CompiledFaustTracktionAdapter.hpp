#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "plugins/compiled/CompiledFaustInterface.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/tracktion/TracktionDeviceAdapters.hpp"

namespace magda {
class DeviceProcessor;
}

namespace magda::daw::audio::compiled {

namespace te = tracktion::engine;

te::Plugin::Ptr createTracktionPlugin(const CompiledPluginSpec& spec,
                                      const te::PluginCreationInfo& info);

/**
 * Resolve a compiled-device slot to the parameter owned by the Tracktion host.
 *
 * Adapter-hosted neutral devices keep their authoritative value on the outer
 * TracktionMagdaDevicePlugin. Legacy Tracktion-native devices own that
 * parameter directly.
 */
te::AutomatableParameter* tracktionParameterForSlot(te::Plugin* plugin, int slotIndex);

std::unique_ptr<magda::DeviceProcessor> createTracktionProcessor(const CompiledPluginSpec& spec,
                                                                 DeviceId deviceId,
                                                                 te::Plugin::Ptr plugin);

}  // namespace magda::daw::audio::compiled
