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
    // The interpreter Faust EFFECT uses the header+grid body layout (isFaust).
    // The Faust INSTRUMENT has its own tabbed custom UI (isFaustInstrument) but
    // shares the Faust chrome-suppression (no standard content header / presets).
    traits.isFaust = kind == magda::InternalDeviceKind::Faust;
    traits.isFaustInstrument = kind == magda::InternalDeviceKind::FaustInstrument;
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
