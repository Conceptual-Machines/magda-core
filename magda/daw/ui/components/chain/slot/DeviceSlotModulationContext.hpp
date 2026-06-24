#pragma once

#include "core/ChainNodePath.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

struct DeviceSlotModulationContext {
    const magda::ModArray* deviceMods = nullptr;
    const magda::MacroArray* deviceMacros = nullptr;
    const magda::ModArray* rackMods = nullptr;
    const magda::MacroArray* rackMacros = nullptr;
    const magda::ModArray* trackMods = nullptr;
    const magda::MacroArray* trackMacros = nullptr;
    int selectedModIndex = -1;
    int selectedMacroIndex = -1;
};

DeviceSlotModulationContext resolveDeviceSlotModulationContext(
    const magda::ChainNodePath& devicePath, const magda::ModArray* deviceMods,
    const magda::MacroArray* deviceMacros, int selectedModIndexOverride,
    int selectedMacroIndexOverride);

}  // namespace magda::daw::ui
