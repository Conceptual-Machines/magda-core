#include "plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"

#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "processors/CompiledFaustProcessor.hpp"

namespace magda::daw::audio::compiled {

te::Plugin::Ptr createTracktionPlugin(const CompiledPluginSpec& spec,
                                      const te::PluginCreationInfo& info) {
    if (spec.createDevice != nullptr) {
        auto device = spec.createDevice(tracktion_adapter::creationContext(info));
        if (device != nullptr)
            return new tracktion_adapter::TracktionMagdaDevicePlugin(info, std::move(device));
    }

    if (spec.createPlugin != nullptr)
        return tracktion_adapter::pluginFromHandle(
            spec.createPlugin(tracktion_adapter::creationContext(info)));

    return {};
}

std::unique_ptr<magda::DeviceProcessor> createTracktionProcessor(const CompiledPluginSpec& spec,
                                                                 DeviceId deviceId,
                                                                 te::Plugin::Ptr plugin) {
    juce::ignoreUnused(spec);

    if (tracktion_adapter::deviceFromPlugin<ICompiledFaustPlugin>(plugin.get()) == nullptr)
        return nullptr;

    return std::make_unique<magda::CompiledFaustProcessor>(deviceId, plugin);
}

}  // namespace magda::daw::audio::compiled
