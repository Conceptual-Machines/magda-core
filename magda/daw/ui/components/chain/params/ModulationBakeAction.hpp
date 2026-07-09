#pragma once

#include <vector>

#include "audio/automation/ModulationBaker.hpp"
#include "core/AutomationCommands.hpp"
#include "params/ParamLinkResolver.hpp"

namespace magda::daw::ui {

/**
 * @brief "Bake Modulation to Automation" (issue #162) — glue between the
 *        param link menu and ModulationBaker / BakeModulationCommand.
 */
struct BakeableModLinks {
    std::vector<magda::ModulationBaker::Source> sources;
    std::vector<magda::BakeModulationCommand::ModLinkRef> linkRefs;

    bool empty() const {
        return sources.empty();
    }
};

/**
 * @brief Collect the parameter's enabled, offline-bakeable (LFO) mod links
 *        across all three scopes, with the scope paths needed to disable
 *        each link after baking.
 */
BakeableModLinks collectBakeableModLinks(const ParamLinkContext& ctx,
                                         const magda::ControlTarget& target);

/**
 * @brief Sample the collected links over the bake range and apply the result
 *        as one undoable BakeModulationCommand.
 *
 * Range: the active time selection, else the loop region when enabled, else
 * the whole timeline. The target's lane is created (and made visible) if
 * missing; its existing automation is the base the modulation is added onto.
 */
void performModulationBake(const magda::ControlTarget& target, BakeableModLinks links);

}  // namespace magda::daw::ui
