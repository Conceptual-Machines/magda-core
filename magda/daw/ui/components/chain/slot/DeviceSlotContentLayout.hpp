#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

class DrumGridUI;
class ParamHostComponent;

struct DeviceSlotContentFrameControls {
    juce::Component* pluginPresetsButton = nullptr;
    juce::Component* levelMeter = nullptr;
    juce::Component* midiNoteStrip = nullptr;
    juce::Component* gainSlider = nullptr;
    juce::Component* paramGrid = nullptr;
    juce::Component* gainLabel = nullptr;
    juce::Component* magdaPresetButton = nullptr;
    juce::Component* activeCustomUI = nullptr;
    juce::Component* compiledPanel = nullptr;
    // Runtime-Faust body components. They live in DeviceSlotContentBodyControls
    // for layout, but the frame pass needs them here so it can hide them when
    // the slot collapses or the plugin has not finished loading — otherwise the
    // FAUST header keeps painting at its last expanded bounds, clipped into the
    // 40px collapsed strip (issue: Faust device shows a sliver of its UI).
    juce::Component* faustHeader = nullptr;
    juce::Component* faustCustomView = nullptr;
    juce::Component* faustMeterPanel = nullptr;
    juce::Component* modButton = nullptr;
    juce::Component* macroButton = nullptr;
    juce::Component* uiButton = nullptr;
    juce::Component* powerButton = nullptr;
    // Small rotary at the top of the meter strip driving wet/dry mix. Only
    // populated when the device exposes a DryGain+WetGain wrapper pair.
    juce::Component* mixKnob = nullptr;
};

struct DeviceSlotContentBodyControls {
    juce::Component* faustHeader = nullptr;
    juce::Component* faustCustomView = nullptr;
    int faustCustomViewPreferredHeight = 0;
    // Strip of read-only readouts a runtime Faust patch declares via
    // bargraphs. Null (or zero height) when the patch declares none, in which
    // case the parameter grid keeps the whole body.
    juce::Component* faustMeterPanel = nullptr;
    int faustMeterPanelPreferredHeight = 0;
    juce::Component* compiledPanel = nullptr;
    int compiledPanelPreferredHeight = 0;
    // Minimum fraction of the slot body the curve panel must occupy.
    // Defaults to 3/4 so curve-heavy plugins (Reverb / Multiband / etc) keep
    // their dominant visual. Plugins that pair a small curve with a deep
    // param grid (the 8-band EQ) override these via CompiledPresentationSpec
    // to let the grid claim the bottom area.
    int compiledPanelMinFractionNumerator = 3;
    int compiledPanelMinFractionDenominator = 4;
    // When true, hide the param grid entirely and give the compiled panel
    // the full slot body. Set per-frame from `CompiledDevicePanel::wantsFullBody()`,
    // which the EQ's "collapse knobs" toggle flips at runtime.
    bool compiledPanelWantsFullBody = false;
    DrumGridUI* drumGridUI = nullptr;
    juce::Component* activeCustomUI = nullptr;
    ParamHostComponent* paramGrid = nullptr;
};

bool prepareDeviceSlotContentFrame(juce::Rectangle<int>& contentArea,
                                   const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                   bool collapsed, bool internalDevice, bool pluginPresetsAvailable,
                                   DeviceSlotContentFrameControls controls, int meterStripWidth,
                                   int contentHeaderHeight);

void layoutDeviceSlotContentBody(juce::Rectangle<int> contentArea, const DeviceSlotTraits& traits,
                                 bool internalDevice, bool hasCustomUI,
                                 DeviceSlotContentBodyControls controls, int faustHeaderHeight);

}  // namespace magda::daw::ui
