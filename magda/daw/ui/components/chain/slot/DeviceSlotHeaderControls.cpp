#include "slot/DeviceSlotHeaderControls.hpp"

#include <algorithm>
#include <vector>

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

enum class HeaderControlId {
    Macro,
    Mod,
    AI,
    Random,
    StepRecord,
    MidiThru,
    Learn,
    UI,
    MultiOut,
    Sidechain,
    ExportClip
};

enum class HeaderControlSide { Left, Right };

struct HeaderControlVisibility {
    bool macro = false;
    bool mod = false;
    bool ai = false;
    bool random = false;
    bool stepRecord = false;
    bool midiThru = false;
    bool learn = false;
    bool ui = false;
    bool multiOut = false;
    bool sidechain = false;
    bool exportClip = false;
    bool power = true;
    bool preset = true;
};

struct HeaderControlSpec {
    HeaderControlId id;
    HeaderControlSide side;
    juce::Component* component = nullptr;
    int expandedOrder = 0;
    int collapsedOrder = 0;
    bool expandedVisible = false;
    bool collapsedVisible = false;
};

struct HeaderControlComponents {
    juce::Component* macroButton = nullptr;
    juce::Component* modButton = nullptr;
    juce::Component* aiButton = nullptr;
    juce::Component* learnButton = nullptr;
    juce::Component* sidechainButton = nullptr;
    juce::Component* multiOutButton = nullptr;
    juce::Component* uiButton = nullptr;
    juce::Component* exportClipButton = nullptr;
    juce::Component* randomButton = nullptr;
    juce::Component* stepRecordButton = nullptr;
    juce::Component* midiThruButton = nullptr;
};

HeaderControlVisibility getHeaderControlVisibility(const DeviceSlotTraits& traits,
                                                   const magda::DeviceInfo& device,
                                                   bool isInternalDevice) {
    HeaderControlVisibility visibility;

    visibility.mod = drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType);
    visibility.macro = visibility.mod || traits.isArpeggiator || traits.isStepSequencer ||
                       traits.isPolyStepSequencer;
    visibility.ai = traits.isAISupported && (visibility.mod || traits.isArpeggiator ||
                                             traits.isStepSequencer || traits.isPolyStepSequencer);
    visibility.random = traits.isStepSequencer || traits.isPolyStepSequencer;
    visibility.stepRecord = visibility.random;
    visibility.midiThru = visibility.random;

    if (isMidiUtility(traits)) {
        visibility.learn = false;
        visibility.sidechain = false;
        visibility.multiOut = false;
        visibility.ui = false;
        visibility.exportClip = true;
        visibility.preset = !traits.isChordEngine;
        return visibility;
    }

    visibility.learn = !isInternalDevice;
    visibility.sidechain = drum_grid_slot::shouldShowSidechainButton(
        traits.isDrumGrid, device.canSidechain, device.canReceiveMidi);
    visibility.multiOut = device.multiOut.isMultiOut;
    visibility.ui = !isInternalDevice || traits.hasAnalyzerPopout;
    visibility.exportClip = false;
    visibility.preset = true;
    return visibility;
}

bool getExpandedVisibility(HeaderControlId id, const HeaderControlVisibility& visibility) {
    switch (id) {
        case HeaderControlId::Macro:
            return visibility.macro;
        case HeaderControlId::Mod:
            return visibility.mod;
        case HeaderControlId::AI:
            return visibility.ai;
        case HeaderControlId::Random:
            return visibility.random;
        case HeaderControlId::StepRecord:
            return visibility.stepRecord;
        case HeaderControlId::MidiThru:
            return visibility.midiThru;
        case HeaderControlId::Learn:
            return visibility.learn;
        case HeaderControlId::UI:
            return visibility.ui;
        case HeaderControlId::MultiOut:
            return visibility.multiOut;
        case HeaderControlId::Sidechain:
            return visibility.sidechain;
        case HeaderControlId::ExportClip:
            return visibility.exportClip;
    }

    return false;
}

bool getCollapsedVisibility(HeaderControlId id, const HeaderControlVisibility& visibility,
                            const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                            bool isInternalDevice) {
    switch (id) {
        case HeaderControlId::Macro:
            return drum_grid_slot::shouldShowMacroButton(
                traits.isDrumGrid, device.deviceType, traits.isArpeggiator,
                traits.isStepSequencer || traits.isPolyStepSequencer);
        case HeaderControlId::Mod:
            return visibility.mod;
        case HeaderControlId::AI:
            return traits.isSoundDesignSupported;
        case HeaderControlId::Random:
            return visibility.random;
        case HeaderControlId::StepRecord:
            return visibility.stepRecord;
        case HeaderControlId::MidiThru:
            return visibility.midiThru;
        case HeaderControlId::UI:
            return drum_grid_slot::shouldShowCollapsedUiButton(traits.isDrumGrid,
                                                               isInternalDevice) ||
                   traits.hasAnalyzerPopout;
        case HeaderControlId::MultiOut:
            return visibility.multiOut;
        case HeaderControlId::ExportClip:
            return visibility.exportClip;
        case HeaderControlId::Learn:
        case HeaderControlId::Sidechain:
            return false;
    }

    return false;
}

std::vector<HeaderControlSpec> buildHeaderControlSpecs(const DeviceSlotTraits& traits,
                                                       const magda::DeviceInfo& device,
                                                       bool isInternalDevice,
                                                       HeaderControlComponents controls) {
    const auto visibility = getHeaderControlVisibility(traits, device, isInternalDevice);

    std::vector<HeaderControlSpec> specs = {
        {HeaderControlId::Macro, HeaderControlSide::Left, controls.macroButton, 10, 20},
        {HeaderControlId::Mod, HeaderControlSide::Left, controls.modButton, 20, 30},
        {HeaderControlId::AI, HeaderControlSide::Left, controls.aiButton, 30, 40},
        {HeaderControlId::Random, HeaderControlSide::Left, controls.randomButton, 40, 50},
        {HeaderControlId::StepRecord, HeaderControlSide::Left, controls.stepRecordButton, 50, 60},
        {HeaderControlId::MidiThru, HeaderControlSide::Left, controls.midiThruButton, 60, 70},
        {HeaderControlId::Learn, HeaderControlSide::Right, controls.learnButton, 70, 0},
        {HeaderControlId::UI, HeaderControlSide::Right, controls.uiButton, 80, 10},
        {HeaderControlId::MultiOut, HeaderControlSide::Right, controls.multiOutButton, 90, 90},
        {HeaderControlId::Sidechain, HeaderControlSide::Right, controls.sidechainButton, 100, 0},
        {HeaderControlId::ExportClip, HeaderControlSide::Right, controls.exportClipButton, 110, 80},
    };

    for (auto& spec : specs) {
        spec.expandedVisible = getExpandedVisibility(spec.id, visibility);
        spec.collapsedVisible =
            getCollapsedVisibility(spec.id, visibility, traits, device, isInternalDevice);
    }

    return specs;
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

HeaderControlComponents getHeaderControlComponents(DeviceSlotCollapsedControls controls) {
    return {.macroButton = controls.macroButton,
            .modButton = controls.modButton,
            .aiButton = controls.aiButton,
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

    auto specs = buildHeaderControlSpecs(traits, device, isInternalDevice,
                                         getHeaderControlComponents(controls));
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
    if (!traits.isArpeggiator && !traits.isStepSequencer && !traits.isPolyStepSequencer)
        setVisibleIfPresent(macroButton, false);
}

}  // namespace magda::daw::ui
