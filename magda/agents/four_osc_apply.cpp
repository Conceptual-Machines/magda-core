#include "four_osc_apply.hpp"

#include "api/plugin_api.hpp"

namespace magda {

juce::String applyFourOscPresetToPath(PluginApi& plugins, const FourOscAgent::Preset& preset,
                                      const ChainNodePath& path) {
    FourOscUpdate update{
        .name = preset.name,
        .category = preset.category,
        .filterType = preset.filterType,
        .voiceMode = preset.voiceMode,
    };
    for (const auto& [name, value] : preset.params)
        update.parameters.emplace(juce::String(name), value);
    for (const auto& [oscillator, wave] : preset.waves)
        update.waves.emplace(oscillator, juce::String(wave));
    for (const auto& [name, enabled] : preset.fx)
        update.effects.emplace(juce::String(name), enabled);
    return plugins.applyFourOscUpdate(path, update);
}

}  // namespace magda
