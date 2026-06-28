#include "slot/DeviceSlotHeaderSpec.hpp"

#include "core/PluginCapabilities.hpp"
#include "drum_grid/DeviceSlotDrumGridBridge.hpp"

namespace magda::daw::ui {
namespace {

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
                traits.isDrumGrid, device.deviceType, traits.isArpeggiator || traits.isStrum,
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

}  // namespace

bool isMidiUtilityDeviceSlot(const DeviceSlotTraits& traits) {
    return traits.isChordEngine || traits.isArpeggiator || traits.isStrum ||
           traits.isStepSequencer || traits.isPolyStepSequencer;
}

HeaderControlVisibility getHeaderControlVisibility(const DeviceSlotTraits& traits,
                                                   const magda::DeviceInfo& device,
                                                   bool isInternalDevice) {
    HeaderControlVisibility visibility;

    visibility.mod = drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType);
    visibility.macro = visibility.mod || traits.isArpeggiator || traits.isStrum ||
                       traits.isStepSequencer || traits.isPolyStepSequencer;
    visibility.ai = traits.isAISupported && (visibility.mod || traits.isArpeggiator ||
                                             traits.isStepSequencer || traits.isPolyStepSequencer);
    visibility.random = traits.isStepSequencer || traits.isPolyStepSequencer;
    visibility.stepRecord = visibility.random;
    visibility.midiThru = supportsMidiSourceToggle(device);

    if (isMidiUtilityDeviceSlot(traits)) {
        visibility.learn = false;
        visibility.sidechain = false;
        visibility.multiOut = false;
        visibility.ui = false;
        visibility.exportClip = true;
        visibility.preset = !traits.isChordEngine;
        return visibility;
    }

    visibility.learn = !isInternalDevice;
    visibility.sidechain = !traits.isDrumGrid && supportsSidechainRoutingMenu(device);
    visibility.multiOut = device.multiOut.isMultiOut;
    visibility.ui = !isInternalDevice || traits.hasAnalyzerPopout;
    visibility.exportClip = false;
    visibility.preset = true;
    return visibility;
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
        {HeaderControlId::Learn, HeaderControlSide::Right, controls.learnButton, 80, 0},
        {HeaderControlId::UI, HeaderControlSide::Right, controls.uiButton, 90, 10},
        {HeaderControlId::MultiOut, HeaderControlSide::Right, controls.multiOutButton, 100, 90},
        {HeaderControlId::Sidechain, HeaderControlSide::Right, controls.sidechainButton, 110, 0},
        {HeaderControlId::ExportClip, HeaderControlSide::Right, controls.exportClipButton, 120, 80},
    };

    for (auto& spec : specs) {
        spec.expandedVisible = getExpandedVisibility(spec.id, visibility);
        spec.collapsedVisible =
            getCollapsedVisibility(spec.id, visibility, traits, device, isInternalDevice);
    }

    return specs;
}

}  // namespace magda::daw::ui
