#include "params/ModulationBakeAction.hpp"

#include <memory>
#include <utility>

#include "core/AutomationManager.hpp"
#include "core/UndoManager.hpp"
#include "state/TimelineController.hpp"

namespace magda::daw::ui {

namespace {

/** Innermost rack enclosing the device path (matches ctx.rackMods' owner). */
magda::ChainNodePath enclosingRackPath(const magda::ChainNodePath& devicePath) {
    auto path = devicePath;
    path.isTrackLevel = false;
    path.topLevelDeviceId = magda::INVALID_DEVICE_ID;
    while (!path.steps.empty() && path.steps.back().type != magda::ChainStepType::Rack)
        path.steps.pop_back();
    return path;
}

void collectFromScope(const magda::ModArray* mods, const magda::ChainNodePath& scopePath,
                      const magda::ControlTarget& target, BakeableModLinks& out) {
    if (mods == nullptr || !scopePath.isValid())
        return;
    for (size_t i = 0; i < mods->size(); ++i) {
        const auto& mod = (*mods)[i];
        if (!mod.enabled || !magda::ModulationBaker::isBakeable(mod))
            continue;
        const auto* link = mod.getLink(target);
        if (link == nullptr || !link->enabled)
            continue;
        out.sources.push_back({mod, *link});
        out.linkRefs.push_back({scopePath, static_cast<int>(i), target});
    }
}

}  // namespace

BakeableModLinks collectBakeableModLinks(const ParamLinkContext& ctx,
                                         const magda::ControlTarget& target) {
    BakeableModLinks out;
    collectFromScope(ctx.deviceMods, ctx.devicePath, target, out);
    collectFromScope(ctx.rackMods, enclosingRackPath(ctx.devicePath), target, out);
    collectFromScope(ctx.trackMods, magda::ChainNodePath::trackLevel(ctx.devicePath.trackId),
                     target, out);
    return out;
}

void performModulationBake(const magda::ControlTarget& target, BakeableModLinks links) {
    if (links.empty())
        return;

    auto* tc = TimelineController::getCurrent();
    if (tc == nullptr || tc->tempoMap() == nullptr)
        return;
    const auto& state = tc->getState();

    magda::ModulationBaker::Options opts;
    if (state.selection.isActive()) {
        opts.startBeat = state.selection.startBeats;
        opts.endBeat = state.selection.endBeats;
    } else if (state.loop.enabled && state.loop.isValid()) {
        opts.startBeat = state.loop.startBeats;
        opts.endBeat = state.loop.endBeats;
    } else {
        opts.startBeat = 0.0;
        opts.endBeat = state.timelineLengthBeats;
    }

    auto& autoMgr = magda::AutomationManager::getInstance();
    const auto laneId = autoMgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
    if (laneId == magda::INVALID_AUTOMATION_LANE_ID)
        return;
    autoMgr.setLaneVisible(laneId, true);

    opts.fallbackBaseValue = autoMgr.getCurrentTargetValue(target).value_or(0.5);
    const auto baseValueAt = [&autoMgr, laneId](double beat) {
        return autoMgr.getValueAtBeat(laneId, beat);
    };

    auto points = magda::ModulationBaker::bake(links.sources, opts, *tc->tempoMap(), baseValueAt);
    if (points.empty())
        return;

    magda::UndoManager::getInstance().executeCommand(std::make_unique<magda::BakeModulationCommand>(
        laneId, opts.startBeat, opts.endBeat, std::move(points), std::move(links.linkRefs)));
}

}  // namespace magda::daw::ui
