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

/** Cycle length in beats shared by ALL sources, or 0 when they disagree.
    Tempo-synced LFOs are beat-periodic by definition; free (Hz) LFOs are
    beat-periodic at a fixed tempo. */
double commonCycleBeats(const std::vector<magda::ModulationBaker::Source>& sources, double bpm) {
    double common = 0.0;
    for (const auto& source : sources) {
        double cycle = 0.0;
        if (source.mod.tempoSync)
            cycle = magda::ModulationBaker::beatsPerCycle(source.mod.syncDivision);
        else if (source.mod.rate > 0.0f)
            cycle = (bpm / 60.0) / static_cast<double>(source.mod.rate);
        if (cycle <= 0.0)
            return 0.0;
        if (common == 0.0)
            common = cycle;
        else if (std::abs(common - cycle) > 1.0e-6)
            return 0.0;
    }
    return common;
}

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
    auto previousLaneState = magda::captureBakeAutomationLaneState(target);
    const auto laneId =
        autoMgr.getOrCreateLane(target, wantClip ? magda::AutomationLaneType::ClipBased
                                                 : magda::AutomationLaneType::Absolute);
    const auto rollbackLanePreparation = [laneId, &previousLaneState]() {
        magda::restoreBakeAutomationLaneState(laneId, previousLaneState);
    };
    // getOrCreateLane returns the existing lane regardless of the requested
    // type. An EMPTY lane of the other type retypes to the chosen
    // destination (the menu offers both for empty lanes); a lane with data
    // of the other kind can't take this bake (menu-gated; this guards the
    // races).
    const auto* lane = autoMgr.getLane(laneId);
    if (lane == nullptr) {
        rollbackLanePreparation();
        return;
    }
    if (lane->isClipBased() != wantClip) {
        if (!autoMgr.retypeEmptyLane(laneId, wantClip ? magda::AutomationLaneType::ClipBased
                                                      : magda::AutomationLaneType::Absolute)) {
            rollbackLanePreparation();
            return;
        }
    }
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

    // Clip bakes: when every source shares one LFO cycle shorter than the
    // range, bake exactly ONE cycle and loop the clip across the range —
    // one crisp cycle of points instead of an unrolled wall.
    const double clipEndBeat = opts.endBeat;
    double loopCycleBeats = 0.0;
    if (clipBased) {
        const double cycle = commonCycleBeats(links.sources, state.tempo.bpm);
        if (cycle > 0.0 && cycle < clipEndBeat - opts.startBeat) {
            loopCycleBeats = cycle;
            opts.endBeat = opts.startBeat + cycle;
        }
    }

    opts.fallbackBaseValue = autoMgr.getCurrentTargetValue(target).value_or(0.5);

    // Ride on the lane's automation only when it actually HAS any: an empty
    // lane's getValueAtBeat falls back to 0.5 (the gap default), which is
    // NOT the knob — baking on it offsets the whole curve and clamps its
    // top. With no lane data, fallbackBaseValue (the live target value)
    // is the base.
    const auto* laneNow = autoMgr.getLane(laneId);
    const bool laneHasData =
        laneNow != nullptr &&
        (laneNow->isClipBased() ? !laneNow->clipIds.empty() : !laneNow->absolutePoints.empty());
    std::function<double(double)> baseValueAt;
    if (laneHasData) {
        baseValueAt = [&autoMgr, laneId](double beat) {
            return autoMgr.getValueAtBeat(laneId, beat);
        };
    }

    auto points = magda::ModulationBaker::bake(links.sources, opts, *tc->tempoMap(), baseValueAt);
    if (points.empty()) {
        rollbackLanePreparation();
        return;
    }

    if (clipBased) {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::BakeModulationToClipCommand>(
                laneId, opts.startBeat, clipEndBeat, std::move(points), std::move(links.linkRefs),
                loopCycleBeats, std::move(previousLaneState)));
    } else {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::BakeModulationCommand>(
                laneId, opts.startBeat, opts.endBeat, std::move(points), std::move(links.linkRefs),
                std::move(previousLaneState)));
    }
}

}  // namespace magda::daw::ui
