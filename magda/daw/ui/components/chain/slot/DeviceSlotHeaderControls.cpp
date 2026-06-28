#include "slot/DeviceSlotHeaderControls.hpp"

#include <algorithm>

#include "slot/DeviceSlotHeaderSpec.hpp"

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

HeaderControlComponents getHeaderControlComponents(DeviceSlotHeaderControls controls) {
    return {.macroButton = controls.macroButton,
            .modButton = controls.modButton,
            .aiButton = controls.aiButton,
            .learnButton = controls.learnButton,
            .sidechainButton = controls.sidechainButton,
            .multiOutButton = controls.multiOutButton,
            .uiButton = controls.uiButton,
            .exportClipButton = controls.exportClipButton,
            .randomButton = controls.randomButton,
            .stepRecordButton = controls.stepRecordButton,
            .midiThruButton = controls.midiThruButton};
}

}  // namespace

void layoutExpandedDeviceSlotHeader(juce::Rectangle<int>& headerArea,
                                    const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                    bool isInternalDevice, DeviceSlotHeaderControls controls,
                                    int buttonSize) {
    setVisibleIfPresent(controls.gainLabel, false);
    const auto visibility = getHeaderControlVisibility(traits, device, isInternalDevice);
    auto specs = buildHeaderControlSpecs(traits, device, isInternalDevice,
                                         getHeaderControlComponents(controls));
    std::sort(specs.begin(), specs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.expandedOrder < rhs.expandedOrder;
    });

    setVisibleIfPresent(controls.powerButton, visibility.power);
    setVisibleIfPresent(controls.presetButton, visibility.preset);

    for (auto& spec : specs) {
        setVisibleIfPresent(spec.component, spec.expandedVisible);

        if (spec.side == HeaderControlSide::Left && spec.expandedVisible)
            placeLeft(headerArea, spec.component, buttonSize);
    }

    for (auto it = specs.rbegin(); it != specs.rend(); ++it) {
        if (it->side == HeaderControlSide::Right && it->expandedVisible)
            placeRight(headerArea, it->component, buttonSize);
    }
}

void layoutCollapsedDeviceSlotControls(juce::Rectangle<int>& area,
                                       juce::Rectangle<int> collapsedMeterArea,
                                       const DeviceSlotTraits& traits,
                                       const magda::DeviceInfo& device, bool isInternalDevice,
                                       DeviceSlotCollapsedControls controls, int maxButtonSize) {
    const bool usesNoteStrip = isMidiUtilityDeviceSlot(traits);
    if (controls.levelMeter != nullptr) {
        controls.levelMeter->setBounds(collapsedMeterArea);
        controls.levelMeter->setVisible(!usesNoteStrip);
    }
    if (controls.midiNoteStrip != nullptr) {
        controls.midiNoteStrip->setBounds(collapsedMeterArea);
        controls.midiNoteStrip->setVisible(usesNoteStrip);
    }

    const int buttonSize = juce::jmin(maxButtonSize, area.getWidth() - 4);

    placeCollapsedButtonIfVisible(area, controls.headerControls.powerButton, true, buttonSize);

    auto specs = buildHeaderControlSpecs(traits, device, isInternalDevice,
                                         getHeaderControlComponents(controls.headerControls));
    std::sort(specs.begin(), specs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.collapsedOrder < rhs.collapsedOrder;
    });

    for (auto& spec : specs) {
        placeCollapsedButtonIfVisible(area, spec.component, spec.collapsedVisible, buttonSize);
    }
}

void applyMidiOnlyDeviceHeaderVisibility(const DeviceSlotTraits& traits,
                                         const magda::DeviceInfo& device,
                                         juce::Component* modButton, juce::Component* macroButton) {
    if (device.deviceType != magda::DeviceType::MIDI)
        return;

    setVisibleIfPresent(modButton, false);
    if (!traits.isArpeggiator && !traits.isStrum && !traits.isStepSequencer &&
        !traits.isPolyStepSequencer)
        setVisibleIfPresent(macroButton, false);
}

}  // namespace magda::daw::ui
