#include "processors/DeviceProcessorFactory.hpp"

#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/tracktion/TracktionDeviceAdapters.hpp"
#include "processors/DeviceProcessor.hpp"
#include "processors/external/ExternalPluginProcessor.hpp"

namespace magda {

std::unique_ptr<DeviceProcessor> createDeviceProcessorForPlugin(
    DeviceId deviceId, tracktion::engine::Plugin::Ptr plugin, const juce::String& pluginId,
    daw::audio::DeviceTrackContext* trackContext) {
    if (!plugin)
        return nullptr;

    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        juce::ignoreUnused(ext);
        auto processor = std::make_unique<ExternalPluginProcessor>(deviceId, plugin, trackContext);
        processor->startParameterListening();
        return processor;
    }

    if (auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(pluginId))
        return daw::audio::compiled::createCompiledPluginProcessor(*compiledSpec, deviceId, plugin);

    if (auto* internalSpec = daw::audio::findInternalPluginSpec(pluginId))
        return daw::audio::createInternalPluginProcessor(
            *internalSpec, deviceId, daw::audio::tracktion_adapter::pluginHandle(plugin));

    return nullptr;
}

}  // namespace magda
