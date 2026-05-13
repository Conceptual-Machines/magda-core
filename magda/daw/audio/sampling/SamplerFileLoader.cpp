#include "sampling/SamplerFileLoader.hpp"

#include "plugin_manager/PluginManager.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"

namespace magda {

SamplerFileLoader::SamplerFileLoader(PluginManager& pluginManager)
    : pluginManager_(pluginManager) {}

bool SamplerFileLoader::loadSample(DeviceId deviceId, const juce::File& file) {
    auto plugin = pluginManager_.getPlugin(deviceId);
    if (!plugin)
        return false;

    auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get());
    if (!sampler)
        return false;

    sampler->loadSample(file);
    return true;
}

}  // namespace magda
