#include "slot/DeviceSlotTraits.hpp"

#include "../../../../../agents/coder_agent.hpp"
#include "../../../../../agents/internal_plugins.hpp"
#include "../../../../../agents/sound_design_agent.hpp"
#include "core/InternalDeviceKind.hpp"

namespace magda::daw::ui {

DeviceSlotTraits makeDeviceSlotTraits(const juce::String& pluginId) {
    const auto kind = magda::classifyInternalDevice(pluginId);

    DeviceSlotTraits traits;
    traits.isDrumGrid = kind == magda::InternalDeviceKind::DrumGrid;
    traits.isChordEngine = kind == magda::InternalDeviceKind::MidiChordEngine;
    traits.isArpeggiator = kind == magda::InternalDeviceKind::Arpeggiator;
    traits.isStepSequencer = kind == magda::InternalDeviceKind::StepSequencer;
    traits.isPolyStepSequencer = kind == magda::InternalDeviceKind::PolyStepSequencer;
    traits.isFaust = kind == magda::InternalDeviceKind::Faust;
    traits.isAnalysis = magda::isAnalysisDevice(pluginId);
    traits.hasAnalyzerPopout = kind == magda::InternalDeviceKind::Oscilloscope ||
                               kind == magda::InternalDeviceKind::SpectrumAnalyzer;
    traits.isAISupported = magda::isDeviceAISupported(pluginId);
    traits.isSoundDesignSupported = magda::isSoundDesignSupported(pluginId);
    traits.isTracktionDevice = magda::isTracktionEngineStockPlugin(pluginId);
    traits.compiledPresentation = findCompiledPresentation(pluginId);
    return traits;
}

}  // namespace magda::daw::ui
