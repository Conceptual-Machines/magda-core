#include "slot/DeviceSlotContentLayout.hpp"

#include "drum_grid/DeviceSlotDrumGridBridge.hpp"

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
    auto stripBounds = contentArea.removeFromRight(meterStripWidth).reduced(1, 3);
    contentArea.removeFromRight(4);

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
        controls.gainSlider->setBounds(stripBounds);
        controls.gainSlider->setVisible(true);
        controls.gainSlider->toFront(false);
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
    setVisibleIfPresent(controls.uiButton, !internalDevice);
    setVisibleIfPresent(controls.powerButton, true);
    setVisibleIfPresent(controls.gainLabel, !isMidiUtility(traits));
}

}  // namespace

bool prepareDeviceSlotContentFrame(juce::Rectangle<int>& contentArea,
                                   const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                   bool collapsed, bool internalDevice, bool pluginPresetsAvailable,
                                   DeviceSlotContentFrameControls controls, int meterStripWidth,
                                   int contentHeaderHeight) {
    if (!collapsed) {
        if (!traits.isFaust) {
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

}  // namespace magda::daw::ui
