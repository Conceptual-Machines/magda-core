#include "processors/DeviceProcessorFactory.hpp"

#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "processors/DeviceProcessor.hpp"

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

    if (dynamic_cast<te::FourOscPlugin*>(plugin.get()))
        return std::make_unique<FourOscProcessor>(deviceId, plugin);
    if (dynamic_cast<te::DelayPlugin*>(plugin.get()))
        return std::make_unique<DelayProcessor>(deviceId, plugin);
    if (dynamic_cast<te::ReverbPlugin*>(plugin.get()))
        return std::make_unique<ReverbProcessor>(deviceId, plugin);
    if (dynamic_cast<te::EqualiserPlugin*>(plugin.get()))
        return std::make_unique<EqualiserProcessor>(deviceId, plugin);
    if (dynamic_cast<te::CompressorPlugin*>(plugin.get()))
        return std::make_unique<CompressorProcessor>(deviceId, plugin);
    if (dynamic_cast<te::ChorusPlugin*>(plugin.get()))
        return std::make_unique<ChorusProcessor>(deviceId, plugin);
    if (dynamic_cast<te::PhaserPlugin*>(plugin.get()))
        return std::make_unique<PhaserProcessor>(deviceId, plugin);
    if (dynamic_cast<te::LowPassPlugin*>(plugin.get()))
        return std::make_unique<FilterProcessor>(deviceId, plugin);
    if (dynamic_cast<te::PitchShiftPlugin*>(plugin.get()))
        return std::make_unique<PitchShiftProcessor>(deviceId, plugin);
    if (dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get()))
        return std::make_unique<ImpulseResponseProcessor>(deviceId, plugin);
    if (dynamic_cast<te::ToneGeneratorPlugin*>(plugin.get()))
        return std::make_unique<ToneGeneratorProcessor>(deviceId, plugin);
    if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin.get()))
        return std::make_unique<UtilityProcessor>(deviceId, plugin);
    if (dynamic_cast<daw::audio::FaustPlugin*>(plugin.get()))
        return std::make_unique<FaustProcessor>(deviceId, plugin);
    if (dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get()))
        return std::make_unique<MagdaSamplerProcessor>(deviceId, plugin);
    if (dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get()))
        return std::make_unique<DrumGridProcessor>(deviceId, plugin);
    if (dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get()))
        return std::make_unique<ArpeggiatorProcessor>(deviceId, plugin);
    if (dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get()))
        return std::make_unique<StepSequencerProcessor>(deviceId, plugin);

    return nullptr;
}

}  // namespace magda
