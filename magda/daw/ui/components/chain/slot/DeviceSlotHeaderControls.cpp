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
    setVisibleIfPresent(controls.uiButton, !isInternalDevice);
    setVisibleIfPresent(controls.presetButton, true);

    placeRight(headerArea, controls.sidechainButton, buttonSize);
    placeRight(headerArea, controls.multiOutButton, buttonSize);
    placeRight(headerArea, controls.uiButton, buttonSize);
    placeRight(headerArea, controls.learnButton, buttonSize);
}

}  // namespace magda::daw::ui
