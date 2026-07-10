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

/** Where a bake lands: absolute lane points, or a new automation clip. */
enum class BakeDestination { Points, Clip };

/**
 * @brief Sample the collected links over the bake range and apply the result
 *        as one undoable command (BakeModulationCommand /
 *        BakeModulationToClipCommand per destination).
 *
 * Range: the active time selection, else the loop region when enabled, else
 * the whole timeline (clip bakes fall back to the track's MIDI content
 * extent instead). The target's lane is created (and made visible) if
 * missing — absolute or clip-based per the destination; an existing lane of
 * the other type makes the bake a no-op (the menu gates on compatibility).
 * The lane's existing automation is the base the modulation is added onto.
 */
void performModulationBake(const magda::ControlTarget& target, BakeableModLinks links,
                           BakeDestination destination);

}  // namespace magda::daw::ui
