#include "sampling/SamplerFileLoader.hpp"

#include "core/ChainNodePath.hpp"
#include "plugin_manager/PluginManager.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/tracktion/SamplerHostBinding.hpp"

namespace magda {

SamplerFileLoader::SamplerFileLoader(PluginManager& pluginManager)
    : pluginManager_(pluginManager) {}

bool SamplerFileLoader::loadSample(const ChainNodePath& devicePath, const juce::File& file) {
    auto plugin = pluginManager_.getPlugin(devicePath);
    if (!plugin)
        return false;

    if (daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MagdaSamplerPlugin>(
            plugin.get()) == nullptr)
        return false;

    daw::audio::tracktion_adapter::loadSamplerSample(*plugin, file);
    return true;
}

}  // namespace magda
