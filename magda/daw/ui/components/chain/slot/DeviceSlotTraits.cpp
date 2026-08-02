#include "slot/DeviceSlotTraits.hpp"

#include "../../../../../agents/internal_plugins.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"

namespace magda::daw::ui {

DeviceSlotTraits makeDeviceSlotTraits(const juce::String& pluginId) {
    DeviceSlotTraits traits;
    traits.isDrumGrid = audio::internalPluginHasTag(pluginId, "drum-grid");
    traits.isChordEngine = audio::internalPluginHasTag(pluginId, "chord-engine");
    traits.isArpeggiator = audio::internalPluginHasTag(pluginId, "arpeggiator");
    traits.isStrum = audio::internalPluginHasTag(pluginId, "strum");
    traits.isStepSequencer = audio::internalPluginHasTag(pluginId, "step-sequencer");
    traits.isPolyStepSequencer = audio::internalPluginHasTag(pluginId, "poly-step-sequencer");
    // Runtime Faust effects and instruments share the header, parameter-grid
    // renderer, and author-group page layout. The flags remain distinct for
    // device-specific behavior outside rendering.
    traits.isFaust = audio::internalPluginHasTag(pluginId, "faust");
    traits.isFaustInstrument = audio::internalPluginHasTag(pluginId, "faust-instrument");
    traits.isAnalysis = audio::isInternalAnalysisPlugin(pluginId);
    traits.hasAnalyzerPopout = audio::internalPluginHasTag(pluginId, "analyzer-popout");
    const auto& agentCapabilities = magda::getInternalPluginCapabilities(pluginId);
    traits.isAISupported = agentCapabilities.supportsDeviceAI();
    traits.isSoundDesignSupported = agentCapabilities.supportsSoundDesign();
    traits.isTracktionDevice = magda::isTracktionEngineStockPlugin(pluginId);
    traits.compiledPresentation = findCompiledPresentation(pluginId);
    return traits;
}

DeviceSlotTraits makeDeviceSlotTraits(const magda::DeviceInfo& device) {
    auto traits = makeDeviceSlotTraits(device.pluginId);
    const bool externalSoundDesign =
        device.format != magda::PluginFormat::Internal && !device.aiSoundDesignerParameters.empty();
    traits.isAISupported = traits.isAISupported || externalSoundDesign;
    traits.isSoundDesignSupported = traits.isSoundDesignSupported || externalSoundDesign;
    return traits;
}

}  // namespace magda::daw::ui
