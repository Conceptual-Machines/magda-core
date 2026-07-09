#include "params/ModulationBakeAction.hpp"

#include <memory>
#include <utility>

#include "core/AutomationManager.hpp"
#include "core/ClipManager.hpp"
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

namespace {

/** End beat of the track's MIDI content (0 if the track has no MIDI clips). */
double midiContentEndBeats(const magda::ControlTarget& target, double bpm) {
    if (target.isEditScoped())
        return 0.0;
    auto& clipManager = magda::ClipManager::getInstance();
    double end = 0.0;
    for (magda::ClipId clipId : clipManager.getClipsOnTrack(target.devicePath.trackId)) {
        const auto* clip = clipManager.getClip(clipId);
        if (clip == nullptr || !clip->isMidi() || clip->view == magda::ClipView::Session)
            continue;
        end = juce::jmax(end, clip->getStartBeats(bpm) + clip->getTimelineLength(bpm) * bpm / 60.0);
    }
    return end;
}

}  // namespace

void performModulationBake(const magda::ControlTarget& target, BakeableModLinks links,
                           BakeDestination destination) {
    if (links.empty())
        return;

    auto* tc = TimelineController::getCurrent();
    if (tc == nullptr || tc->tempoMap() == nullptr)
        return;
    const auto& state = tc->getState();

    const bool wantClip = destination == BakeDestination::Clip;
    auto& autoMgr = magda::AutomationManager::getInstance();
    const auto laneId =
        autoMgr.getOrCreateLane(target, wantClip ? magda::AutomationLaneType::ClipBased
                                                 : magda::AutomationLaneType::Absolute);
    // getOrCreateLane returns the existing lane regardless of the requested
    // type; an existing lane of the OTHER type can't take this bake (the
    // menu gates on compatibility, this guards the races).
    const auto* lane = autoMgr.getLane(laneId);
    if (lane == nullptr || lane->isClipBased() != wantClip)
        return;
    const bool clipBased = wantClip;
    autoMgr.setLaneVisible(laneId, true);

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
        if (clipBased) {
            // A clip spanning the whole timeline is unusable; without a
            // selection or loop, bake across the track's MIDI content.
            const double contentEnd = midiContentEndBeats(target, state.tempo.bpm);
            if (contentEnd > 0.0)
                opts.endBeat = contentEnd;
        }
    }

    opts.fallbackBaseValue = autoMgr.getCurrentTargetValue(target).value_or(0.5);
    const auto baseValueAt = [&autoMgr, laneId](double beat) {
        return autoMgr.getValueAtBeat(laneId, beat);
    };

    auto points = magda::ModulationBaker::bake(links.sources, opts, *tc->tempoMap(), baseValueAt);
    if (points.empty())
        return;

    if (clipBased) {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::BakeModulationToClipCommand>(laneId, opts.startBeat,
                                                                 opts.endBeat, std::move(points),
                                                                 std::move(links.linkRefs)));
    } else {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::BakeModulationCommand>(laneId, opts.startBeat, opts.endBeat,
                                                           std::move(points),
                                                           std::move(links.linkRefs)));
    }
}

}  // namespace magda::daw::ui
