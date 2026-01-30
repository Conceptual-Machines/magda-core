#pragma once

#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

/**
 * @brief Context for resolving mod/macro links to a specific parameter.
 *
 * Built once from ParamSlotComponent state, then passed to pure query functions
 * that don't need access to the component itself.
 */
struct ParamLinkContext {
    magda::DeviceId deviceId = magda::INVALID_DEVICE_ID;
    int paramIndex = -1;
    magda::ChainNodePath devicePath;
    const magda::ModArray* deviceMods = nullptr;
    const magda::ModArray* rackMods = nullptr;
    const magda::MacroArray* deviceMacros = nullptr;
    const magda::MacroArray* rackMacros = nullptr;
    int selectedModIndex = -1;    // -1 = show all
    int selectedMacroIndex = -1;  // -1 = show all
};

/**
 * @brief A resolved mod link returned by value (no static temporaries).
 */
struct ResolvedModLink {
    int modIndex;
    magda::ModLink link;  // Copied by value — safe across threads
};

/**
 * @brief A resolved macro link returned by value.
 */
struct ResolvedMacroLink {
    int macroIndex;
    magda::MacroLink link;
};

// Pure query functions — no side effects, no Component access
std::vector<ResolvedModLink> getLinkedMods(const ParamLinkContext& ctx);
std::vector<ResolvedMacroLink> getLinkedMacros(const ParamLinkContext& ctx);
bool hasActiveLinks(const ParamLinkContext& ctx);
float computeTotalModModulation(const ParamLinkContext& ctx);
float computeTotalMacroModulation(const ParamLinkContext& ctx);

/**
 * @brief Resolve a ModSelection to a concrete ModInfo pointer.
 *
 * Uses the parentPath in the selection to decide whether the mod is
 * device-level or rack-level, then validates the index.
 */
const magda::ModInfo* resolveModPtr(const magda::ModSelection& sel,
                                    const magda::ChainNodePath& devicePath,
                                    const magda::ModArray* deviceMods,
                                    const magda::ModArray* rackMods);

/**
 * @brief Resolve a MacroSelection to a concrete MacroInfo pointer.
 */
const magda::MacroInfo* resolveMacroPtr(const magda::MacroSelection& sel,
                                        const magda::ChainNodePath& devicePath,
                                        const magda::MacroArray* deviceMacros,
                                        const magda::MacroArray* rackMacros);

/**
 * @brief Check if a device path is within the scope of a parent path.
 *
 * Used to determine whether a parameter should respond to link-mode
 * events from a given mod/macro parent.
 */
bool isInScopeOf(const magda::ChainNodePath& devicePath, const magda::ChainNodePath& parentPath);

}  // namespace magda::daw::ui
