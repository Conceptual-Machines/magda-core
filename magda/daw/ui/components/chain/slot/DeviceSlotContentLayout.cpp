#include "slot/DeviceSlotContentLayout.hpp"

#include "drum_grid/DeviceSlotDrumGridBridge.hpp"
#include "params/ParamHostComponent.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

void setVisibleIfPresent(juce::Component* component, bool shouldBeVisible) {
    if (component != nullptr)
        component->setVisible(shouldBeVisible);
}

bool isMidiUtility(const DeviceSlotTraits& traits) {
    return traits.isChordEngine || traits.isArpeggiator || traits.isStepSequencer;
}

void layoutPluginPresetButton(juce::Rectangle<int> secondHeaderArea, const DeviceSlotTraits& traits,
                              bool pluginPresetsAvailable, juce::Component* pluginPresetsButton) {
    if (pluginPresetsButton == nullptr)
        return;

    const bool eligible = !isMidiUtility(traits);
    const bool show = eligible && pluginPresetsAvailable;
    if (!show) {
        pluginPresetsButton->setVisible(false);
        return;
    }

    const int btnWidth = juce::jmin(140, secondHeaderArea.getWidth() / 2);
    pluginPresetsButton->setBounds(secondHeaderArea.removeFromRight(btnWidth).reduced(2, 3));
    pluginPresetsButton->setVisible(true);
}

void layoutMeterStrip(juce::Rectangle<int>& contentArea, const DeviceSlotTraits& traits,
                      DeviceSlotContentFrameControls controls, int meterStripWidth) {
    // Width 0 = no meter strip (e.g. post-FX analysis devices): reserve nothing
    // so the body uses the full width.
    if (meterStripWidth <= 0) {
        setVisibleIfPresent(controls.levelMeter, false);
        setVisibleIfPresent(controls.midiNoteStrip, false);
        setVisibleIfPresent(controls.gainSlider, false);
        return;
    }

    auto stripBounds = contentArea.removeFromRight(meterStripWidth).reduced(1, 3);
    contentArea.removeFromRight(4);

    // Mix knob at the very top of the meter strip when the host wires one in.
    // The meter (and the overlaid gain slider) shrink to leave room. Visible
    // only when the device has a DryGain+WetGain wrapper pair — that decision
    // lives on the host, which sets the knob's visibility before relayout.
    constexpr int kMixKnobHeight = 18;
    if (controls.mixKnob != nullptr && controls.mixKnob->isVisible() &&
        stripBounds.getHeight() > kMixKnobHeight + 8) {
        controls.mixKnob->setBounds(stripBounds.removeFromTop(kMixKnobHeight));
        stripBounds.removeFromTop(2);
        controls.mixKnob->toFront(false);
    }

    const bool usesNoteStrip = isMidiUtility(traits);
    if (controls.levelMeter != nullptr) {
        controls.levelMeter->setBounds(stripBounds);
        controls.levelMeter->setVisible(!usesNoteStrip);
    }
    if (controls.midiNoteStrip != nullptr) {
        controls.midiNoteStrip->setBounds(stripBounds);
        controls.midiNoteStrip->setVisible(usesNoteStrip);
    }

    if (controls.gainSlider != nullptr) {
        // Analysis devices (oscilloscope / spectrum) are transparent passthroughs:
        // no volume control. The level meter still shows.
        if (traits.isAnalysis) {
            controls.gainSlider->setVisible(false);
        } else {
            controls.gainSlider->setBounds(stripBounds);
            controls.gainSlider->setVisible(true);
            controls.gainSlider->toFront(false);
        }
    }
}

void hideBodyControls(DeviceSlotContentFrameControls controls, bool collapsed) {
    setVisibleIfPresent(controls.paramGrid, false);
    setVisibleIfPresent(controls.gainLabel, false);
    setVisibleIfPresent(controls.pluginPresetsButton, false);
    setVisibleIfPresent(controls.gainSlider, false);
    setVisibleIfPresent(controls.magdaPresetButton, !collapsed);
    setVisibleIfPresent(controls.activeCustomUI, false);
    setVisibleIfPresent(controls.compiledPanel, false);
}

void showExpandedHeaderControls(const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                bool internalDevice, DeviceSlotContentFrameControls controls) {
    setVisibleIfPresent(controls.modButton,
                        drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType));
    setVisibleIfPresent(controls.macroButton, drum_grid_slot::shouldShowMacroButton(
                                                  traits.isDrumGrid, device.deviceType,
                                                  traits.isArpeggiator, traits.isStepSequencer));
    setVisibleIfPresent(controls.uiButton, !internalDevice || traits.isAnalysis);
    setVisibleIfPresent(controls.powerButton, true);
    setVisibleIfPresent(controls.gainLabel, !isMidiUtility(traits) && !traits.isAnalysis);
}

void layoutParamGrid(ParamHostComponent* paramGrid, juce::Rectangle<int> area) {
    if (paramGrid == nullptr)
        return;

    auto labelFont =
        FontManager::getInstance().getUIFont(DebugSettings::getInstance().getParamLabelFontSize());
    auto valueFont =
        FontManager::getInstance().getUIFont(DebugSettings::getInstance().getParamValueFontSize());
    paramGrid->setBounds(area);
    paramGrid->setVisible(true);
    paramGrid->layoutContent(labelFont, valueFont);
}

int boundedBottomPanelHeight(int preferredHeight, int bodyHeight, int minFractionNumerator,
                             int minFractionDenominator) {
    jassert(minFractionNumerator >= 0);
    jassert(minFractionDenominator > 0);

    const int minScaledHeight = (bodyHeight * minFractionNumerator) / minFractionDenominator;
    return juce::jlimit(juce::jmin(preferredHeight, bodyHeight), bodyHeight,
                        juce::jmax(preferredHeight, minScaledHeight));
}

}  // namespace

bool prepareDeviceSlotContentFrame(juce::Rectangle<int>& contentArea,
                                   const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                   bool collapsed, bool internalDevice, bool pluginPresetsAvailable,
                                   DeviceSlotContentFrameControls controls, int meterStripWidth,
                                   int contentHeaderHeight) {
    const bool skipContentHeader = traits.isAnalysis || traits.isFaust ||
                                   (traits.compiledPresentation != nullptr &&
                                    traits.compiledPresentation->layoutCellCount == 0);

    if (!collapsed) {
        if (!skipContentHeader) {
            auto secondHeaderArea = contentArea.removeFromTop(contentHeaderHeight);
            layoutPluginPresetButton(secondHeaderArea, traits, pluginPresetsAvailable,
                                     controls.pluginPresetsButton);
        } else {
            setVisibleIfPresent(controls.pluginPresetsButton, false);
        }

        layoutMeterStrip(contentArea, traits, controls, meterStripWidth);
    }

    contentArea.removeFromBottom(2);

    if (collapsed || device.loadState != magda::DeviceLoadState::Loaded) {
        hideBodyControls(controls, collapsed);
        return false;
    }

    showExpandedHeaderControls(traits, device, internalDevice, controls);
    return true;
}

void layoutDeviceSlotContentBody(juce::Rectangle<int> contentArea, const DeviceSlotTraits& traits,
                                 bool internalDevice, bool hasCustomUI,
                                 DeviceSlotContentBodyControls controls, int faustHeaderHeight) {
    if (traits.isFaust && controls.faustHeader != nullptr) {
        controls.faustHeader->setBounds(contentArea.removeFromTop(faustHeaderHeight));
        controls.faustHeader->setVisible(true);

        if (controls.faustCustomView != nullptr) {
            const int bodyHeight = juce::jmax(0, contentArea.getHeight());
            const int customHeight =
                boundedBottomPanelHeight(controls.faustCustomViewPreferredHeight, bodyHeight, 1, 2);
            controls.faustCustomView->setBounds(contentArea.removeFromBottom(customHeight));
            controls.faustCustomView->setVisible(true);
        }

        layoutParamGrid(controls.paramGrid, contentArea);
        return;
    }

    if (controls.compiledPanel != nullptr) {
        const int bodyHeight = juce::jmax(0, contentArea.getHeight());

        if (controls.compiledPanelWantsFullBody) {
            // Panel wants the entire body — hide the param grid and let the
            // panel paint the whole content area. The EQ's collapse toggle
            // uses this path.
            controls.compiledPanel->setBounds(contentArea);
            controls.compiledPanel->setVisible(true);
            setVisibleIfPresent(controls.paramGrid, false);
            return;
        }

        const int visualHeight =
            boundedBottomPanelHeight(controls.compiledPanelPreferredHeight, bodyHeight,
                                     controls.compiledPanelMinFractionNumerator,
                                     controls.compiledPanelMinFractionDenominator);
        controls.compiledPanel->setBounds(contentArea.removeFromBottom(visualHeight));
        controls.compiledPanel->setVisible(true);

        layoutParamGrid(controls.paramGrid, contentArea);
        return;
    }

    if (internalDevice && hasCustomUI) {
        if (!drum_grid_slot::layoutDrumGridUI(controls.drumGridUI, contentArea) &&
            controls.activeCustomUI != nullptr) {
            controls.activeCustomUI->setBounds(contentArea.reduced(4));
            controls.activeCustomUI->setVisible(true);
        }

        setVisibleIfPresent(controls.paramGrid, false);
        return;
    }

    setVisibleIfPresent(controls.activeCustomUI, false);
    layoutParamGrid(controls.paramGrid, contentArea);
}

}  // namespace magda::daw::ui
