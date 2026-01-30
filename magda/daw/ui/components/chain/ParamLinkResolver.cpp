#include "ParamLinkResolver.hpp"

namespace magda::daw::ui {

std::vector<ResolvedModLink> getLinkedMods(const ParamLinkContext& ctx) {
    std::vector<ResolvedModLink> linked;
    if (!ctx.deviceMods || ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return linked;
    }

    magda::ModTarget thisTarget{ctx.deviceId, ctx.paramIndex};

    // If a mod is selected, only check that specific mod
    if (ctx.selectedModIndex >= 0 &&
        ctx.selectedModIndex < static_cast<int>(ctx.deviceMods->size())) {
        const auto& mod = (*ctx.deviceMods)[static_cast<size_t>(ctx.selectedModIndex)];
        if (const auto* link = mod.getLink(thisTarget)) {
            linked.push_back({ctx.selectedModIndex, *link});
        }
        // Legacy check: old single-target field
        else if (mod.target.deviceId == ctx.deviceId && mod.target.paramIndex == ctx.paramIndex) {
            magda::ModLink legacyLink;
            legacyLink.target = mod.target;
            legacyLink.amount = mod.amount;
            linked.push_back({ctx.selectedModIndex, legacyLink});
        }
        return linked;
    }

    // No mod selected - show all linked mods
    for (size_t i = 0; i < ctx.deviceMods->size(); ++i) {
        const auto& mod = (*ctx.deviceMods)[i];
        if (const auto* link = mod.getLink(thisTarget)) {
            linked.push_back({static_cast<int>(i), *link});
        }
        // Legacy: also check old target field
        else if (mod.target.deviceId == ctx.deviceId && mod.target.paramIndex == ctx.paramIndex) {
            magda::ModLink legacyLink;
            legacyLink.target = mod.target;
            legacyLink.amount = mod.amount;
            linked.push_back({static_cast<int>(i), legacyLink});
        }
    }
    return linked;
}

std::vector<ResolvedMacroLink> getLinkedMacros(const ParamLinkContext& ctx) {
    std::vector<ResolvedMacroLink> linked;
    if (!ctx.deviceMacros || ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return linked;
    }

    magda::MacroTarget thisTarget{ctx.deviceId, ctx.paramIndex};

    // If a macro is selected, only check that specific macro
    if (ctx.selectedMacroIndex >= 0 &&
        ctx.selectedMacroIndex < static_cast<int>(ctx.deviceMacros->size())) {
        const auto& macro = (*ctx.deviceMacros)[static_cast<size_t>(ctx.selectedMacroIndex)];
        if (const auto* link = macro.getLink(thisTarget)) {
            linked.push_back({ctx.selectedMacroIndex, *link});
        }
        return linked;
    }

    // No macro selected - show all linked macros
    for (size_t i = 0; i < ctx.deviceMacros->size(); ++i) {
        const auto& macro = (*ctx.deviceMacros)[i];
        if (const auto* link = macro.getLink(thisTarget)) {
            linked.push_back({static_cast<int>(i), *link});
        }
    }
    return linked;
}

bool hasActiveLinks(const ParamLinkContext& ctx) {
    if (ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return false;
    }

    magda::ModTarget modTarget{ctx.deviceId, ctx.paramIndex};

    // Check device-level mods
    if (ctx.deviceMods) {
        for (const auto& mod : *ctx.deviceMods) {
            if (mod.getLink(modTarget) != nullptr) {
                return true;
            }
        }
    }

    // Check rack-level mods
    if (ctx.rackMods) {
        for (const auto& mod : *ctx.rackMods) {
            if (mod.getLink(modTarget) != nullptr) {
                return true;
            }
        }
    }

    // Check device-level macros
    magda::MacroTarget macroTarget{ctx.deviceId, ctx.paramIndex};
    if (ctx.deviceMacros) {
        for (const auto& macro : *ctx.deviceMacros) {
            if (macro.getLink(macroTarget) != nullptr) {
                return true;
            }
        }
    }

    // Check rack-level macros
    if (ctx.rackMacros) {
        for (const auto& macro : *ctx.rackMacros) {
            if (macro.getLink(macroTarget) != nullptr) {
                return true;
            }
        }
    }

    return false;
}

float computeTotalModModulation(const ParamLinkContext& ctx) {
    float total = 0.0f;
    if (ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return total;
    }

    magda::ModTarget modTarget{ctx.deviceId, ctx.paramIndex};

    // Device-level mods
    if (ctx.deviceMods) {
        for (const auto& mod : *ctx.deviceMods) {
            if (const auto* link = mod.getLink(modTarget)) {
                total += mod.value * link->amount;
            }
        }
    }

    // Rack-level mods
    if (ctx.rackMods) {
        for (const auto& mod : *ctx.rackMods) {
            if (const auto* link = mod.getLink(modTarget)) {
                total += mod.value * link->amount;
            }
        }
    }

    return total;
}

float computeTotalMacroModulation(const ParamLinkContext& ctx) {
    float total = 0.0f;
    if (ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return total;
    }

    magda::MacroTarget macroTarget{ctx.deviceId, ctx.paramIndex};

    // Device-level macros
    if (ctx.deviceMacros) {
        for (const auto& macro : *ctx.deviceMacros) {
            if (const auto* link = macro.getLink(macroTarget)) {
                total += macro.value * link->amount;
            }
        }
    }

    // Rack-level macros
    if (ctx.rackMacros) {
        for (const auto& macro : *ctx.rackMacros) {
            if (const auto* link = macro.getLink(macroTarget)) {
                total += macro.value * link->amount;
            }
        }
    }

    return total;
}

const magda::ModInfo* resolveModPtr(const magda::ModSelection& sel,
                                    const magda::ChainNodePath& devicePath,
                                    const magda::ModArray* deviceMods,
                                    const magda::ModArray* rackMods) {
    if (!sel.isValid() || sel.modIndex < 0) {
        return nullptr;
    }

    if (sel.parentPath == devicePath && deviceMods &&
        sel.modIndex < static_cast<int>(deviceMods->size())) {
        return &(*deviceMods)[static_cast<size_t>(sel.modIndex)];
    }

    if (rackMods && sel.modIndex < static_cast<int>(rackMods->size())) {
        return &(*rackMods)[static_cast<size_t>(sel.modIndex)];
    }

    return nullptr;
}

const magda::MacroInfo* resolveMacroPtr(const magda::MacroSelection& sel,
                                        const magda::ChainNodePath& devicePath,
                                        const magda::MacroArray* deviceMacros,
                                        const magda::MacroArray* rackMacros) {
    if (!sel.isValid() || sel.macroIndex < 0) {
        return nullptr;
    }

    if (sel.parentPath == devicePath && deviceMacros &&
        sel.macroIndex < static_cast<int>(deviceMacros->size())) {
        return &(*deviceMacros)[static_cast<size_t>(sel.macroIndex)];
    }

    if (rackMacros && sel.macroIndex < static_cast<int>(rackMacros->size())) {
        return &(*rackMacros)[static_cast<size_t>(sel.macroIndex)];
    }

    return nullptr;
}

bool isInScopeOf(const magda::ChainNodePath& devicePath, const magda::ChainNodePath& parentPath) {
    // Must be on the same track
    if (devicePath.trackId != parentPath.trackId) {
        return false;
    }

    bool parentIsTopLevel = parentPath.topLevelDeviceId != magda::INVALID_DEVICE_ID;
    bool deviceIsTopLevel = devicePath.topLevelDeviceId != magda::INVALID_DEVICE_ID;

    // Case 1: Parent is a top-level device
    if (parentIsTopLevel) {
        if (!deviceIsTopLevel) {
            return false;
        }
        return devicePath.topLevelDeviceId == parentPath.topLevelDeviceId;
    }

    // Case 2: Parent uses steps (rack/chain/device inside rack)
    if (parentPath.steps.empty()) {
        return false;
    }

    if (deviceIsTopLevel) {
        return false;
    }

    auto parentType = parentPath.getType();

    if (parentType == magda::ChainNodeType::Rack) {
        // Parameter must be in a device inside that rack (a descendant)
        if (devicePath.steps.size() <= parentPath.steps.size()) {
            return false;
        }
        for (size_t i = 0; i < parentPath.steps.size(); ++i) {
            if (devicePath.steps[i].type != parentPath.steps[i].type ||
                devicePath.steps[i].id != parentPath.steps[i].id) {
                return false;
            }
        }
        return true;
    } else if (parentType == magda::ChainNodeType::Device) {
        // Parameter must belong to that exact device
        if (devicePath.steps.size() != parentPath.steps.size()) {
            return false;
        }
        for (size_t i = 0; i < parentPath.steps.size(); ++i) {
            if (devicePath.steps[i].type != parentPath.steps[i].type ||
                devicePath.steps[i].id != parentPath.steps[i].id) {
                return false;
            }
        }
        return true;
    }

    return false;
}

}  // namespace magda::daw::ui
