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

std::unique_ptr<magda::DeviceProcessor> createTracktionProcessor(const CompiledPluginSpec& spec,
                                                                 DeviceId deviceId,
                                                                 te::Plugin::Ptr plugin);

}  // namespace magda::daw::audio::compiled
