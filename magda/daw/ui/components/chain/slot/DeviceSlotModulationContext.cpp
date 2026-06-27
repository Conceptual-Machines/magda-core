#include "slot/DeviceSlotModulationContext.hpp"

#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "modulation/ModulationOwnerPath.hpp"

namespace magda::daw::ui {

DeviceSlotModulationContext resolveDeviceSlotModulationContext(
    const magda::ChainNodePath& devicePath, const magda::ModArray* deviceMods,
    const magda::MacroArray* deviceMacros, int selectedModIndexOverride,
    int selectedMacroIndexOverride) {
    DeviceSlotModulationContext context;
    context.deviceMods = deviceMods;
    context.deviceMacros = deviceMacros;

    if (auto rackPath = nearestRackPathForDevicePath(devicePath); rackPath.isValid()) {
        if (auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath)) {
            context.rackMods = &rack->mods;
            context.rackMacros = &rack->macros;
        }
    }

    if (devicePath.trackId != magda::INVALID_TRACK_ID) {
        const auto* trackInfo = magda::TrackManager::getInstance().getTrack(devicePath.trackId);
        if (trackInfo != nullptr) {
            context.trackMods = &trackInfo->mods;
            context.trackMacros = &trackInfo->macros;
        }
    }

    auto& selectionManager = magda::SelectionManager::getInstance();
    if (selectionManager.hasModSelection()) {
        const auto& modSelection = selectionManager.getModSelection();
        if (modSelection.parentPath == devicePath)
            context.selectedModIndex = modSelection.modIndex;
    }
    if (selectedModIndexOverride >= 0)
        context.selectedModIndex = selectedModIndexOverride;

    if (selectionManager.hasMacroSelection()) {
        const auto& macroSelection = selectionManager.getMacroSelection();
        if (macroSelection.parentPath == devicePath)
            context.selectedMacroIndex = macroSelection.macroIndex;
    }
    if (selectedMacroIndexOverride >= 0)
        context.selectedMacroIndex = selectedMacroIndexOverride;

    return context;
}

}  // namespace magda::daw::ui
