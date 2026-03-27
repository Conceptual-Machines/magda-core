#include "ParamLinkResolver.hpp"

namespace magda::daw::ui {

std::vector<ResolvedModLink> getLinkedMods(const ParamLinkContext& ctx) {
    std::vector<ResolvedModLink> linked;
    if (ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return linked;
    }

    magda::ModTarget thisTarget{ctx.deviceId, ctx.paramIndex};

    // If a mod is selected, only check that specific mod
    if (ctx.selectedModIndex >= 0 && ctx.deviceMods &&
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

    // No mod selected - show all linked mods from all scopes
    if (ctx.deviceMods) {
        for (size_t i = 0; i < ctx.deviceMods->size(); ++i) {
            const auto& mod = (*ctx.deviceMods)[i];
            if (const auto* link = mod.getLink(thisTarget)) {
                linked.push_back({static_cast<int>(i), *link, ResolvedModLink::Scope::Device});
            }
            // Legacy: also check old target field
            else if (mod.target.deviceId == ctx.deviceId &&
                     mod.target.paramIndex == ctx.paramIndex) {
                magda::ModLink legacyLink;
                legacyLink.target = mod.target;
                legacyLink.amount = mod.amount;
                linked.push_back({static_cast<int>(i), legacyLink, ResolvedModLink::Scope::Device});
            }
        }
    }
    if (ctx.rackMods) {
        for (size_t i = 0; i < ctx.rackMods->size(); ++i) {
            const auto& mod = (*ctx.rackMods)[i];
            if (const auto* link = mod.getLink(thisTarget)) {
                linked.push_back({static_cast<int>(i), *link, ResolvedModLink::Scope::Rack});
            }
        }
    }
    if (ctx.trackMods) {
        for (size_t i = 0; i < ctx.trackMods->size(); ++i) {
            const auto& mod = (*ctx.trackMods)[i];
            if (const auto* link = mod.getLink(thisTarget)) {
                linked.push_back({static_cast<int>(i), *link, ResolvedModLink::Scope::Track});
            }
        }
    }
    return linked;
}

std::vector<ResolvedMacroLink> getLinkedMacros(const ParamLinkContext& ctx) {
    std::vector<ResolvedMacroLink> linked;
    if (ctx.deviceId == magda::INVALID_DEVICE_ID) {
        return linked;
    }

    magda::MacroTarget thisTarget{ctx.deviceId, ctx.paramIndex};

    // If a macro is selected, only check that specific macro
    if (ctx.selectedMacroIndex >= 0 && ctx.deviceMacros &&
        ctx.selectedMacroIndex < static_cast<int>(ctx.deviceMacros->size())) {
        const auto& macro = (*ctx.deviceMacros)[static_cast<size_t>(ctx.selectedMacroIndex)];
        if (const auto* link = macro.getLink(thisTarget)) {
            linked.push_back({ctx.selectedMacroIndex, *link});
        }
        return linked;
    }

    // No macro selected - show all linked macros from all scopes
    if (ctx.deviceMacros) {
        for (size_t i = 0; i < ctx.deviceMacros->size(); ++i) {
            const auto& macro = (*ctx.deviceMacros)[i];
            if (const auto* link = macro.getLink(thisTarget)) {
                linked.push_back({static_cast<int>(i), *link, ResolvedMacroLink::Scope::Device});
            }
        }
    }
    if (ctx.rackMacros) {
        for (size_t i = 0; i < ctx.rackMacros->size(); ++i) {
            const auto& macro = (*ctx.rackMacros)[i];
            if (const auto* link = macro.getLink(thisTarget)) {
                linked.push_back({static_cast<int>(i), *link, ResolvedMacroLink::Scope::Rack});
            }
        }
    }
    if (ctx.trackMacros) {
        for (size_t i = 0; i < ctx.trackMacros->size(); ++i) {
            const auto& macro = (*ctx.trackMacros)[i];
            if (const auto* link = macro.getLink(thisTarget)) {
                linked.push_back({static_cast<int>(i), *link, ResolvedMacroLink::Scope::Track});
            }
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

    // Check track-level mods
    if (ctx.trackMods) {
        for (const auto& mod : *ctx.trackMods) {
            if (mod.getLink(modTarget) != nullptr) {
                return true;
            }
        }
    }

    // Check track-level macros
    if (ctx.trackMacros) {
        for (const auto& macro : *ctx.trackMacros) {
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
                float modOffset = link->bipolar ? (mod.value * 2.0f - 1.0f) : mod.value;
                total += modOffset * link->amount;
            }
        }
    }

    // Rack-level mods
    if (ctx.rackMods) {
        for (const auto& mod : *ctx.rackMods) {
            if (const auto* link = mod.getLink(modTarget)) {
                float modOffset = link->bipolar ? (mod.value * 2.0f - 1.0f) : mod.value;
                total += modOffset * link->amount;
            }
        }
    }

    // Track-level mods
    if (ctx.trackMods) {
        for (const auto& mod : *ctx.trackMods) {
            if (const auto* link = mod.getLink(modTarget)) {
                float modOffset = link->bipolar ? (mod.value * 2.0f - 1.0f) : mod.value;
                total += modOffset * link->amount;
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
                float macroOffset = link->bipolar ? (macro.value * 2.0f - 1.0f) : macro.value;
                total += macroOffset * link->amount;
            }
        }
    }

    // Rack-level macros
    if (ctx.rackMacros) {
        for (const auto& macro : *ctx.rackMacros) {
            if (const auto* link = macro.getLink(macroTarget)) {
                float macroOffset = link->bipolar ? (macro.value * 2.0f - 1.0f) : macro.value;
                total += macroOffset * link->amount;
            }
        }
    }

    // Track-level macros
    if (ctx.trackMacros) {
        for (const auto& macro : *ctx.trackMacros) {
            if (const auto* link = macro.getLink(macroTarget)) {
                float macroOffset = link->bipolar ? (macro.value * 2.0f - 1.0f) : macro.value;
                total += macroOffset * link->amount;
            }
        }
    }

    return total;
}

const magda::ModInfo* resolveModPtr(const magda::ModSelection& sel,
                                    const magda::ChainNodePath& devicePath,
                                    const magda::ModArray* deviceMods,
                                    const magda::ModArray* rackMods,
                                    const magda::ModArray* trackMods) {
    if (!sel.isValid() || sel.modIndex < 0) {
        return nullptr;
    }

    if (sel.parentPath == devicePath && deviceMods &&
        sel.modIndex < static_cast<int>(deviceMods->size())) {
        return &(*deviceMods)[static_cast<size_t>(sel.modIndex)];
    }

    if (rackMods && sel.modIndex < static_cast<int>(rackMods->size())) {
        // Check if parentPath is a rack path (not track-level)
        if (!sel.parentPath.isTrackLevel) {
            return &(*rackMods)[static_cast<size_t>(sel.modIndex)];
        }
    }

    if (trackMods && sel.parentPath.isTrackLevel &&
        sel.modIndex < static_cast<int>(trackMods->size())) {
        return &(*trackMods)[static_cast<size_t>(sel.modIndex)];
    }

    return nullptr;
}

const magda::MacroInfo* resolveMacroPtr(const magda::MacroSelection& sel,
                                        const magda::ChainNodePath& devicePath,
                                        const magda::MacroArray* deviceMacros,
                                        const magda::MacroArray* rackMacros,
                                        const magda::MacroArray* trackMacros) {
    if (!sel.isValid() || sel.macroIndex < 0) {
        return nullptr;
    }

    if (sel.parentPath == devicePath && deviceMacros &&
        sel.macroIndex < static_cast<int>(deviceMacros->size())) {
        return &(*deviceMacros)[static_cast<size_t>(sel.macroIndex)];
    }

    if (rackMacros && sel.macroIndex < static_cast<int>(rackMacros->size())) {
        if (!sel.parentPath.isTrackLevel) {
            return &(*rackMacros)[static_cast<size_t>(sel.macroIndex)];
        }
    }

    if (trackMacros && sel.parentPath.isTrackLevel &&
        sel.macroIndex < static_cast<int>(trackMacros->size())) {
        return &(*trackMacros)[static_cast<size_t>(sel.macroIndex)];
    }

    return nullptr;
}

bool isInScopeOf(const magda::ChainNodePath& devicePath, const magda::ChainNodePath& parentPath) {
    // Must be on the same track
    if (devicePath.trackId != parentPath.trackId) {
        return false;
    }

    // Track-level parent: all devices on the same track are in scope
    if (parentPath.isTrackLevel) {
        return true;
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
