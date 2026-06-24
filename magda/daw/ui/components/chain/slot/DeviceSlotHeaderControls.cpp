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

void placeCollapsedButtonIfVisible(juce::Rectangle<int>& area, juce::Component* component,
                                   bool shouldBeVisible, int buttonSize) {
    setVisibleIfPresent(component, shouldBeVisible);

    if (shouldBeVisible)
        placeCollapsedButton(area, component, buttonSize);
}

bool isMidiUtility(const DeviceSlotTraits& traits) {
    return traits.isChordEngine || traits.isArpeggiator || traits.isStepSequencer ||
           traits.isPolyStepSequencer;
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
    } else if (traits.isArpeggiator || traits.isStepSequencer || traits.isPolyStepSequencer) {
        placeLeft(headerArea, controls.macroButton, buttonSize);
        setVisibleIfPresent(controls.modButton, false);
        placeAIButton();
    } else {
        setVisibleIfPresent(controls.macroButton, false);
        setVisibleIfPresent(controls.modButton, false);
        setVisibleIfPresent(controls.aiButton, false);
    }

    // Step-sequencer action buttons in the header, next to the AI button:
    // randomize, step record, MIDI thru.
    if (traits.isStepSequencer || traits.isPolyStepSequencer) {
        setVisibleIfPresent(controls.randomButton, true);
        setVisibleIfPresent(controls.stepRecordButton, true);
        setVisibleIfPresent(controls.midiThruButton, true);
        placeLeft(headerArea, controls.randomButton, buttonSize);
        placeLeft(headerArea, controls.stepRecordButton, buttonSize);
        placeLeft(headerArea, controls.midiThruButton, buttonSize);
    } else {
        setVisibleIfPresent(controls.randomButton, false);
        setVisibleIfPresent(controls.stepRecordButton, false);
        setVisibleIfPresent(controls.midiThruButton, false);
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
    // Analysis devices have no native editor, but the UI button pops the
    // oscilloscope/spectrum into a floating window. Levels has no popout, so it
    // is gated on hasAnalyzerPopout rather than isAnalysis.
    setVisibleIfPresent(controls.uiButton, !isInternalDevice || traits.hasAnalyzerPopout);
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

    placeCollapsedButtonIfVisible(area, controls.powerButton, true, buttonSize);

    const bool showUI =
        drum_grid_slot::shouldShowCollapsedUiButton(traits.isDrumGrid, isInternalDevice) ||
        traits.hasAnalyzerPopout;
    placeCollapsedButtonIfVisible(area, controls.uiButton, showUI, buttonSize);

    const bool showMod = drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType);
    const bool showMacro = drum_grid_slot::shouldShowMacroButton(
        traits.isDrumGrid, device.deviceType, traits.isArpeggiator,
        traits.isStepSequencer || traits.isPolyStepSequencer);
    placeCollapsedButtonIfVisible(area, controls.macroButton, showMacro, buttonSize);
    placeCollapsedButtonIfVisible(area, controls.modButton, showMod, buttonSize);

    if (traits.isSoundDesignSupported) {
        area.removeFromTop(4);
        if (controls.aiButton != nullptr)
            controls.aiButton->setBounds(
                area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        setVisibleIfPresent(controls.aiButton, true);
    } else {
        setVisibleIfPresent(controls.aiButton, false);
    }

    const bool showSequencerActions = traits.isStepSequencer || traits.isPolyStepSequencer;
    if (showSequencerActions) {
        placeCollapsedButtonIfVisible(area, controls.randomButton, true, buttonSize);
        placeCollapsedButtonIfVisible(area, controls.stepRecordButton, true, buttonSize);
        placeCollapsedButtonIfVisible(area, controls.midiThruButton, true, buttonSize);
    } else {
        setVisibleIfPresent(controls.randomButton, false);
        setVisibleIfPresent(controls.stepRecordButton, false);
        setVisibleIfPresent(controls.midiThruButton, false);
    }

    placeCollapsedButtonIfVisible(area, controls.exportClipButton, isMidiUtility(traits),
                                  buttonSize);

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
    if (!traits.isArpeggiator && !traits.isStepSequencer && !traits.isPolyStepSequencer)
        setVisibleIfPresent(macroButton, false);
}

}  // namespace magda::daw::ui
