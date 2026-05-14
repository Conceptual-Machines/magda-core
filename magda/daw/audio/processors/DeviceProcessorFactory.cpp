#include "processors/DeviceProcessorFactory.hpp"

#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "processors/DeviceProcessor.hpp"
#include "processors/external/ExternalPluginProcessor.hpp"

namespace magda {

std::unique_ptr<DeviceProcessor> createDeviceProcessorForPlugin(
    DeviceId deviceId, tracktion::engine::Plugin::Ptr plugin, const juce::String& pluginId) {
    if (!plugin)
        return nullptr;

    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        juce::ignoreUnused(ext);
        auto processor = std::make_unique<ExternalPluginProcessor>(deviceId, plugin);
        processor->startParameterListening();
        return processor;
    }

    if (auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(pluginId))
        return daw::audio::compiled::createCompiledPluginProcessor(*compiledSpec, deviceId, plugin);

    if (auto* internalSpec = daw::audio::findInternalPluginSpec(pluginId))
        return daw::audio::createInternalPluginProcessor(*internalSpec, deviceId, plugin);

    return nullptr;
}

}  // namespace magda
