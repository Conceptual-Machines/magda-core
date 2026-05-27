#include "slot/DeviceSlotHeaderControls.hpp"

#include "drum_grid/DeviceSlotDrumGridBridge.hpp"

namespace magda::daw::ui {

namespace {

void setVisibleIfPresent(juce::Component* component, bool shouldBeVisible) {
    if (component != nullptr)
        component->setVisible(shouldBeVisible);
}

void placeLeft(juce::Rectangle<int>& area, juce::Component* component, int buttonSize) {
    if (component == nullptr)
        return;

    component->setBounds(area.removeFromLeft(buttonSize));
    area.removeFromLeft(4);
}

void placeRight(juce::Rectangle<int>& area, juce::Component* component, int buttonSize) {
    if (component == nullptr || !component->isVisible())
        return;

    component->setBounds(area.removeFromRight(buttonSize));
    area.removeFromRight(4);
}

void placeCollapsedButton(juce::Rectangle<int>& area, juce::Component* component, int buttonSize) {
    if (component == nullptr)
        return;

    component->setBounds(
        area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    area.removeFromTop(4);
}

bool isMidiUtility(const DeviceSlotTraits& traits) {
    return traits.isChordEngine || traits.isArpeggiator || traits.isStepSequencer;
}

}  // namespace

void layoutExpandedDeviceSlotHeader(juce::Rectangle<int>& headerArea,
                                    const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                    bool isInternalDevice, DeviceSlotHeaderControls controls,
                                    int buttonSize) {
    setVisibleIfPresent(controls.gainLabel, false);

    const auto placeAIButton = [&] {
        if (traits.isAISupported) {
            setVisibleIfPresent(controls.aiButton, true);
            placeLeft(headerArea, controls.aiButton, buttonSize);
        } else {
            setVisibleIfPresent(controls.aiButton, false);
        }
    };

    if (drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType)) {
        placeLeft(headerArea, controls.macroButton, buttonSize);
        placeLeft(headerArea, controls.modButton, buttonSize);
        placeAIButton();
    } else if (traits.isArpeggiator || traits.isStepSequencer) {
        placeLeft(headerArea, controls.macroButton, buttonSize);
        setVisibleIfPresent(controls.modButton, false);
        placeAIButton();
    } else {
        setVisibleIfPresent(controls.macroButton, false);
        setVisibleIfPresent(controls.modButton, false);
        setVisibleIfPresent(controls.aiButton, false);
    }

    if (isMidiUtility(traits)) {
        setVisibleIfPresent(controls.learnButton, false);
        setVisibleIfPresent(controls.sidechainButton, false);
        setVisibleIfPresent(controls.multiOutButton, false);
        setVisibleIfPresent(controls.powerButton, true);
        setVisibleIfPresent(controls.presetButton, !traits.isChordEngine);
        setVisibleIfPresent(controls.exportClipButton, true);

        placeRight(headerArea, controls.exportClipButton, buttonSize);
        return;
    }

    setVisibleIfPresent(controls.exportClipButton, false);
    setVisibleIfPresent(controls.sidechainButton,
                        drum_grid_slot::shouldShowSidechainButton(
                            traits.isDrumGrid, device.canSidechain, device.canReceiveMidi));
    setVisibleIfPresent(controls.multiOutButton, device.multiOut.isMultiOut);
    setVisibleIfPresent(controls.learnButton, !isInternalDevice);
    setVisibleIfPresent(controls.powerButton, true);
    // Analysis devices have no native editor, but the UI button pops their
    // oscilloscope/spectrum into a floating window.
    setVisibleIfPresent(controls.uiButton, !isInternalDevice || traits.isAnalysis);
    setVisibleIfPresent(controls.presetButton, true);

    placeRight(headerArea, controls.sidechainButton, buttonSize);
    placeRight(headerArea, controls.multiOutButton, buttonSize);
    placeRight(headerArea, controls.uiButton, buttonSize);
    placeRight(headerArea, controls.learnButton, buttonSize);
}

void layoutCollapsedDeviceSlotControls(juce::Rectangle<int>& area,
                                       juce::Rectangle<int> collapsedMeterArea,
                                       const DeviceSlotTraits& traits,
                                       const magda::DeviceInfo& device, bool isInternalDevice,
                                       DeviceSlotCollapsedControls controls, int maxButtonSize) {
    const bool usesNoteStrip = isMidiUtility(traits);
    if (controls.levelMeter != nullptr) {
        controls.levelMeter->setBounds(collapsedMeterArea);
        controls.levelMeter->setVisible(!usesNoteStrip);
    }
    if (controls.midiNoteStrip != nullptr) {
        controls.midiNoteStrip->setBounds(collapsedMeterArea);
        controls.midiNoteStrip->setVisible(usesNoteStrip);
    }

    const int buttonSize = juce::jmin(maxButtonSize, area.getWidth() - 4);

    placeCollapsedButton(area, controls.powerButton, buttonSize);
    setVisibleIfPresent(controls.powerButton, true);

    const bool showUI =
        drum_grid_slot::shouldShowCollapsedUiButton(traits.isDrumGrid, isInternalDevice) ||
        traits.isAnalysis;
    if (showUI) {
        placeCollapsedButton(area, controls.uiButton, buttonSize);
        setVisibleIfPresent(controls.uiButton, true);
    } else {
        setVisibleIfPresent(controls.uiButton, false);
    }

    const bool showMod = drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType);
    const bool showMacro = drum_grid_slot::shouldShowMacroButton(
        traits.isDrumGrid, device.deviceType, traits.isArpeggiator, traits.isStepSequencer);
    placeCollapsedButton(area, controls.macroButton, buttonSize);
    setVisibleIfPresent(controls.macroButton, showMacro);

    if (controls.modButton != nullptr)
        controls.modButton->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
    setVisibleIfPresent(controls.modButton, showMod);

    if (traits.isSoundDesignSupported) {
        area.removeFromTop(4);
        if (controls.aiButton != nullptr)
            controls.aiButton->setBounds(
                area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        setVisibleIfPresent(controls.aiButton, true);
    } else {
        setVisibleIfPresent(controls.aiButton, false);
    }

    if (device.multiOut.isMultiOut && controls.multiOutButton != nullptr) {
        area.removeFromTop(4);
        controls.multiOutButton->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        controls.multiOutButton->setVisible(true);
    }
}

void applyMidiOnlyDeviceHeaderVisibility(const DeviceSlotTraits& traits,
                                         const magda::DeviceInfo& device,
                                         juce::Component* modButton, juce::Component* macroButton) {
    if (device.deviceType != magda::DeviceType::MIDI)
        return;

    setVisibleIfPresent(modButton, false);
    if (!traits.isArpeggiator && !traits.isStepSequencer)
        setVisibleIfPresent(macroButton, false);
}

}  // namespace magda::daw::ui
