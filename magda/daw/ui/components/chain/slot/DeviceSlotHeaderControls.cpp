#include "slot/DeviceSlotHeaderControls.hpp"

#include <algorithm>
#include <ranges>

#include "slot/DeviceSlotHeaderSpec.hpp"

namespace magda::daw::ui {

namespace {

void setVisibleIfPresent(juce::Component* component, bool shouldBeVisible) {
    if (component != nullptr)
        component->setVisible(shouldBeVisible);
}

void placeLeft(juce::Rectangle<int>& area, juce::Component* component,
               const DeviceSlotHeaderMetrics& m) {
    if (component == nullptr)
        return;

    component->setBounds(
        area.removeFromLeft(m.buttonWidth).withSizeKeepingCentre(m.buttonWidth, m.buttonHeight));
    area.removeFromLeft(m.gap);
}

void placeRight(juce::Rectangle<int>& area, juce::Component* component,
                const DeviceSlotHeaderMetrics& m) {
    if (component == nullptr || !component->isVisible())
        return;

    component->setBounds(
        area.removeFromRight(m.buttonWidth).withSizeKeepingCentre(m.buttonWidth, m.buttonHeight));
    area.removeFromRight(m.gap);
}

bool livesOutsideExpandedHeader(HeaderControlId id) {
    return id == HeaderControlId::Sidechain || id == HeaderControlId::MultiOut ||
           id == HeaderControlId::MidiThru || id == HeaderControlId::Delta;
}

juce::Rectangle<int> takeSeparator(juce::Rectangle<int>& area, bool fromLeft,
                                   const DeviceSlotHeaderMetrics& m) {
    const int width = (2 * m.separatorMargin) + 1;
    auto slot = fromLeft ? area.removeFromLeft(width) : area.removeFromRight(width);
    return slot.withSizeKeepingCentre(1, m.separatorHeight);
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
            .midiThruButton = controls.midiThruButton,
            .deltaButton = controls.deltaButton};
}

}  // namespace

DeviceSlotHeaderSeparators layoutExpandedDeviceSlotHeader(juce::Rectangle<int>& headerArea,
                                                          const DeviceSlotTraits& traits,
                                                          const magda::DeviceInfo& device,
                                                          bool isInternalDevice,
                                                          DeviceSlotHeaderControls controls,
                                                          const DeviceSlotHeaderMetrics& metrics) {
    DeviceSlotHeaderSeparators separators;
    setVisibleIfPresent(controls.gainLabel, false);
    const auto visibility = getHeaderControlVisibility(traits, device, isInternalDevice);
    auto specs = buildHeaderControlSpecs(traits, device, isInternalDevice,
                                         getHeaderControlComponents(controls));
    constexpr auto expandedOrder = [](const auto& spec) { return spec.expandedOrder; };
    std::ranges::sort(specs, {}, expandedOrder);

    setVisibleIfPresent(controls.powerButton, visibility.power);
    setVisibleIfPresent(controls.presetButton, visibility.preset);

    bool placedAnyLeft = false;
    for (auto& spec : specs) {
        setVisibleIfPresent(spec.component, spec.expandedVisible);

        if (spec.side == HeaderControlSide::Left && spec.expandedVisible &&
            !livesOutsideExpandedHeader(spec.id)) {
            placeLeft(headerArea, spec.component, metrics);
            placedAnyLeft = true;
        }
    }
    if (placedAnyLeft) {
        // The separator carries its own margins in place of the last gap.
        headerArea.setLeft(headerArea.getX() - metrics.gap);
        separators.left = takeSeparator(headerArea, true, metrics);
    }

    const auto placedRight = [](const auto& spec) {
        return spec.side == HeaderControlSide::Right && spec.expandedVisible &&
               !livesOutsideExpandedHeader(spec.id);
    };
    bool separatorTaken = false;
    for (auto& spec : specs | std::views::reverse | std::views::filter(placedRight)) {
        if (!separatorTaken && spec.component != nullptr && spec.component->isVisible()) {
            headerArea.setRight(headerArea.getRight() + metrics.gap);
            separators.right = takeSeparator(headerArea, false, metrics);
            separatorTaken = true;
        }
        placeRight(headerArea, spec.component, metrics);
    }
    return separators;
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
    constexpr auto collapsedOrder = [](const auto& spec) { return spec.collapsedOrder; };
    std::ranges::sort(specs, {}, collapsedOrder);

    for (auto& spec : specs) {
        placeCollapsedButtonIfVisible(area, spec.component, spec.collapsedVisible, buttonSize);
    }
}

void applyMidiOnlyDeviceHeaderVisibility(const DeviceSlotTraits& traits,
                                         const magda::DeviceInfo& device,
                                         juce::Component* modButton, juce::Component* macroButton) {
    // The Chord Engine by name as well as by type: it stopped being a
    // DeviceType::MIDI device when it was declared for what it is (#2427), and
    // what it has to modulate did not change -- nothing.
    if (device.deviceType != magda::DeviceType::MIDI && !traits.isChordEngine)
        return;

    setVisibleIfPresent(modButton, false);
    if (!traits.isArpeggiator && !traits.isStrum && !traits.isStepSequencer &&
        !traits.isPolyStepSequencer)
        setVisibleIfPresent(macroButton, false);
}

}  // namespace magda::daw::ui
