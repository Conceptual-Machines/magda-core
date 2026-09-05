#include "AutomationCommands.hpp"

#include <algorithm>
#include <ranges>

#include "LinkModeManager.hpp"
#include "TrackManager.hpp"

namespace magda {
namespace {

bool pointIsInDuplicateRange(double beatPosition, double startBeat, double endBeat) {
    constexpr double epsilon = 1.0e-9;
    return beatPosition >= startBeat - epsilon && beatPosition <= endBeat + epsilon;
}

}  // namespace

// ============================================================================
// Helper: find a point in a lane or clip
// ============================================================================

static const AutomationPoint* findPointInLane(AutomationLaneId laneId, AutomationPointId pointId) {
    auto* lane = AutomationManager::getInstance().getLane(laneId);
    if (!lane)
        return nullptr;
    for (const auto& p : lane->absolutePoints)
        if (p.id == pointId)
            return &p;
    return nullptr;
}

static const AutomationPoint* findPointInClip(AutomationClipId clipId, AutomationPointId pointId) {
    auto* clip = AutomationManager::getInstance().getClip(clipId);
    if (!clip)
        return nullptr;
    for (const auto& p : clip->points)
        if (p.id == pointId)
            return &p;
    return nullptr;
}

static const AutomationPoint* findPoint(bool isClip, AutomationLaneId laneId,
                                        AutomationClipId clipId, AutomationPointId pointId) {
    return isClip ? findPointInClip(clipId, pointId) : findPointInLane(laneId, pointId);
}

// ============================================================================
// AddAutomationPointCommand
// ============================================================================

void AddAutomationPointCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        addedPointId_ = mgr.addPointToClip(clipId_, beatPosition_, value_, curveType_);
    else
        addedPointId_ = mgr.addPoint(laneId_, beatPosition_, value_, curveType_);
}

void AddAutomationPointCommand::undo() {
    auto& mgr = AutomationManager::getInstance();
    if (addedPointId_ != INVALID_AUTOMATION_POINT_ID) {
        if (isClip_)
            mgr.deletePointFromClip(clipId_, addedPointId_);
        else
            mgr.deletePoint(laneId_, addedPointId_);
    }
}

// ============================================================================
// DeleteAutomationPointCommand
// ============================================================================

void DeleteAutomationPointCommand::capturePoint() {
    const auto* pt = findPoint(isClip_, laneId_, clipId_, pointId_);
    if (pt)
        storedPoint_ = *pt;
}

void DeleteAutomationPointCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.deletePointFromClip(clipId_, pointId_);
    else
        mgr.deletePoint(laneId_, pointId_);
}

void DeleteAutomationPointCommand::undo() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_) {
        AutomationPointId newId = mgr.addPointToClip(clipId_, storedPoint_.beatPosition,
                                                     storedPoint_.value, storedPoint_.curveType);
        // Restore tension and handles on the re-created point
        if (newId != INVALID_AUTOMATION_POINT_ID) {
            mgr.setPointTensionInClip(clipId_, newId, storedPoint_.tension);
            mgr.setPointHandlesInClip(clipId_, newId, storedPoint_.inHandle,
                                      storedPoint_.outHandle);
            pointId_ = newId;  // Update for potential redo
        }
    } else {
        AutomationPointId newId = mgr.addPoint(laneId_, storedPoint_.beatPosition,
                                               storedPoint_.value, storedPoint_.curveType);
        if (newId != INVALID_AUTOMATION_POINT_ID) {
            mgr.setPointTension(laneId_, newId, storedPoint_.tension);
            mgr.setPointHandles(laneId_, newId, storedPoint_.inHandle, storedPoint_.outHandle);
            pointId_ = newId;
        }
    }
}

// ============================================================================
// MoveAutomationPointCommand
// ============================================================================

void MoveAutomationPointCommand::captureOldPosition() {
    const auto* pt = findPoint(isClip_, laneId_, clipId_, pointId_);
    if (pt) {
        oldBeatPosition_ = pt->beatPosition;
        oldValue_ = pt->value;
    }
}

void MoveAutomationPointCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.movePointInClip(clipId_, pointId_, newBeatPosition_, newValue_);
    else
        mgr.movePoint(laneId_, pointId_, newBeatPosition_, newValue_);
}

void MoveAutomationPointCommand::undo() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.movePointInClip(clipId_, pointId_, oldBeatPosition_, oldValue_);
    else
        mgr.movePoint(laneId_, pointId_, oldBeatPosition_, oldValue_);
}

// ============================================================================
// SetAutomationPointTensionCommand
// ============================================================================

void SetAutomationPointTensionCommand::captureOldTension() {
    const auto* pt = findPoint(isClip_, laneId_, clipId_, pointId_);
    if (pt)
        oldTension_ = pt->tension;
}

void SetAutomationPointTensionCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.setPointTensionInClip(clipId_, pointId_, newTension_);
    else
        mgr.setPointTension(laneId_, pointId_, newTension_);
}

void SetAutomationPointTensionCommand::undo() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.setPointTensionInClip(clipId_, pointId_, oldTension_);
    else
        mgr.setPointTension(laneId_, pointId_, oldTension_);
}

// ============================================================================
// SetAutomationPointHandlesCommand
// ============================================================================

void SetAutomationPointHandlesCommand::captureOldHandles() {
    const auto* pt = findPoint(isClip_, laneId_, clipId_, pointId_);
    if (pt) {
        oldInHandle_ = pt->inHandle;
        oldOutHandle_ = pt->outHandle;
    }
}

void SetAutomationPointHandlesCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.setPointHandlesInClip(clipId_, pointId_, newInHandle_, newOutHandle_);
    else
        mgr.setPointHandles(laneId_, pointId_, newInHandle_, newOutHandle_);
}

void SetAutomationPointHandlesCommand::undo() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.setPointHandlesInClip(clipId_, pointId_, oldInHandle_, oldOutHandle_);
    else
        mgr.setPointHandles(laneId_, pointId_, oldInHandle_, oldOutHandle_);
}

// ============================================================================
// SetAutomationPointCurveTypeCommand
// ============================================================================

void SetAutomationPointCurveTypeCommand::captureOldCurveType() {
    const auto* pt = findPoint(isClip_, laneId_, clipId_, pointId_);
    if (pt)
        oldCurveType_ = pt->curveType;
}

void SetAutomationPointCurveTypeCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.setPointCurveTypeInClip(clipId_, pointId_, newCurveType_);
    else
        mgr.setPointCurveType(laneId_, pointId_, newCurveType_);
}

void SetAutomationPointCurveTypeCommand::undo() {
    auto& mgr = AutomationManager::getInstance();
    if (isClip_)
        mgr.setPointCurveTypeInClip(clipId_, pointId_, oldCurveType_);
    else
        mgr.setPointCurveType(laneId_, pointId_, oldCurveType_);
}

// ============================================================================
// DeleteAutomationLaneCommand
// ============================================================================

void DeleteAutomationLaneCommand::captureLane() {
    auto& mgr = AutomationManager::getInstance();
    const auto& lanes = mgr.getLanes();
    for (size_t i = 0; i < lanes.size(); ++i) {
        if (lanes[i].id != laneId_)
            continue;
        storedLane_ = lanes[i];
        storedIndex_ = i;
        if (storedLane_.isClipBased()) {
            for (auto clipId : storedLane_.clipIds) {
                if (const auto* clip = mgr.getClip(clipId))
                    storedClips_.push_back(*clip);
            }
        }
        captured_ = true;
        return;
    }
}

void DeleteAutomationLaneCommand::execute() {
    AutomationManager::getInstance().deleteLane(laneId_);
}

void DeleteAutomationLaneCommand::undo() {
    if (!captured_)
        return;
    auto& mgr = AutomationManager::getInstance();
    AutomationLaneInfo laneCopy = storedLane_;
    mgr.insertLaneAt(laneCopy, storedIndex_);
    for (const auto& clip : storedClips_) {
        AutomationClipInfo clipCopy = clip;
        mgr.restoreClip(clipCopy);
    }
}

// ============================================================================
// DuplicateAutomationTimeSelectionCommand
// ============================================================================

bool DuplicateAutomationTimeSelectionCommand::shouldDuplicateLane(
    const AutomationLaneInfo& lane) const {
    if (!lane.visible || !lane.isAbsolute())
        return false;
    // Tempo is rippled directly on edit.tempoSequence (see TempoSequenceRippleCommand).
    if (lane.target.kind == ControlTarget::Kind::Tempo)
        return false;
    if (!laneIds_.empty()) {
        return std::find(laneIds_.begin(), laneIds_.end(), lane.id) != laneIds_.end();
    }
    if (trackIds_.empty())
        return true;
    return std::find(trackIds_.begin(), trackIds_.end(), lane.target.devicePath.trackId) !=
           trackIds_.end();
}

// ============================================================================
// ConvertAutomationLaneTypeCommand
// ============================================================================

void ConvertAutomationLaneTypeCommand::captureLane() {
    auto& mgr = AutomationManager::getInstance();
    const auto* lane = mgr.getLane(laneId_);
    if (!lane)
        return;

    storedLane_ = *lane;
    storedClips_.clear();
    for (auto clipId : lane->clipIds) {
        if (const auto* clip = mgr.getClip(clipId))
            storedClips_.push_back(*clip);
    }
    captured_ = true;
}

void ConvertAutomationLaneTypeCommand::execute() {
    if (!captured_)
        return;
    auto& mgr = AutomationManager::getInstance();
    if (storedLane_.isAbsolute())
        mgr.convertLaneToClipBased(laneId_, clipMinLengthBeats_);
    else
        mgr.convertLaneToAbsolute(laneId_);
}

void ConvertAutomationLaneTypeCommand::undo() {
    if (!captured_)
        return;
    AutomationManager::getInstance().restoreLaneState(storedLane_, storedClips_);
}

// ============================================================================
// Clip commands
// ============================================================================

void CreateAutomationClipCommand::execute() {
    auto& mgr = AutomationManager::getInstance();
    createdClipId_ = mgr.createClip(laneId_, startBeats_, lengthBeats_);
    if (createdClipId_ == INVALID_AUTOMATION_CLIP_ID)
        return;

    // Seed a straight line across the clip at the target's current value, so
    // a freshly drawn clip starts as "hold what the parameter is now"
    // instead of empty. Model-level createClip stays a bare primitive.
    double seedValue = 0.5;
    if (const auto* lane = mgr.getLane(laneId_)) {
        if (auto current = mgr.getCurrentTargetValue(lane->target))
            seedValue = juce::jlimit(0.0, 1.0, *current);
    }
    mgr.addPointToClip(createdClipId_, 0.0, seedValue);
    mgr.addPointToClip(createdClipId_, lengthBeats_, seedValue);
}

void CreateAutomationClipCommand::undo() {
    if (createdClipId_ != INVALID_AUTOMATION_CLIP_ID)
        AutomationManager::getInstance().deleteClip(createdClipId_);
}

void DeleteAutomationClipCommand::captureClip() {
    if (const auto* clip = AutomationManager::getInstance().getClip(clipId_)) {
        storedClip_ = *clip;
        captured_ = true;
    }
}

void DeleteAutomationClipCommand::execute() {
    if (captured_)
        AutomationManager::getInstance().deleteClip(clipId_);
}

void DeleteAutomationClipCommand::undo() {
    if (!captured_)
        return;
    AutomationClipInfo clipCopy = storedClip_;
    AutomationManager::getInstance().restoreClip(clipCopy);
}

void MoveAutomationClipCommand::captureOldStart() {
    if (const auto* clip = AutomationManager::getInstance().getClip(clipId_))
        oldStartBeats_ = clip->startBeats;
}

void MoveAutomationClipCommand::execute() {
    AutomationManager::getInstance().moveClip(clipId_, newStartBeats_);
}

void MoveAutomationClipCommand::undo() {
    AutomationManager::getInstance().moveClip(clipId_, oldStartBeats_);
}

void RenameAutomationClipCommand::captureOldName() {
    if (const auto* clip = AutomationManager::getInstance().getClip(clipId_))
        oldName_ = clip->name;
}

void RenameAutomationClipCommand::execute() {
    AutomationManager::getInstance().setClipName(clipId_, newName_);
}

void RenameAutomationClipCommand::undo() {
    AutomationManager::getInstance().setClipName(clipId_, oldName_);
}

void SetAutomationClipColourCommand::captureOldColour() {
    if (const auto* clip = AutomationManager::getInstance().getClip(clipId_))
        oldColour_ = clip->colour;
}

void SetAutomationClipColourCommand::execute() {
    AutomationManager::getInstance().setClipColour(clipId_, newColour_);
}

void SetAutomationClipColourCommand::undo() {
    AutomationManager::getInstance().setClipColour(clipId_, oldColour_);
}

void ResizeAutomationClipCommand::captureOldBounds() {
    if (const auto* clip = AutomationManager::getInstance().getClip(clipId_)) {
        oldStartBeats_ = clip->startBeats;
        oldLengthBeats_ = clip->lengthBeats;
    }
}

void ResizeAutomationClipCommand::execute() {
    AutomationManager::getInstance().resizeClip(clipId_, newLengthBeats_, fromStart_);
}

void ResizeAutomationClipCommand::undo() {
    // Restore both bounds: a fromStart resize moved the start too.
    auto& mgr = AutomationManager::getInstance();
    mgr.moveClip(clipId_, oldStartBeats_);
    mgr.resizeClip(clipId_, oldLengthBeats_, false);
}

void DuplicateAutomationClipCommand::execute() {
    createdClipId_ = AutomationManager::getInstance().duplicateClip(sourceClipId_);
}

void DuplicateAutomationClipCommand::undo() {
    if (createdClipId_ != INVALID_AUTOMATION_CLIP_ID)
        AutomationManager::getInstance().deleteClip(createdClipId_);
}

// ============================================================================
// BakeModulationCommand
// ============================================================================

namespace {

std::vector<AutomationClipInfo> clipsForLane(const AutomationManager& manager,
                                             const AutomationLaneInfo& lane) {
    std::vector<AutomationClipInfo> clips;
    clips.reserve(lane.clipIds.size());
    for (const auto clipId : lane.clipIds) {
        if (const auto* clip = manager.getClip(clipId))
            clips.push_back(*clip);
    }
    return clips;
}

void restoreBakeLaneState(const BakeAutomationLaneState& state) {
    if (!state.captured || !state.laneExisted)
        return;

    auto& manager = AutomationManager::getInstance();
    if (manager.getLane(state.lane.id) != nullptr) {
        manager.restoreLaneState(state.lane, state.clips);
        return;
    }

    auto lane = state.lane;
    manager.restoreLane(lane);
    for (auto clip : state.clips)
        manager.restoreClip(clip);
}

bool prepareBakeLaneForExecute(AutomationLaneId laneId, AutomationLaneType expectedType,
                               const BakeAutomationLaneState& previousState,
                               BakeAutomationLaneState& preparedState) {
    auto& manager = AutomationManager::getInstance();

    if (previousState.captured && preparedState.captured) {
        restoreBakeLaneState(preparedState);
    } else if (previousState.captured) {
        const auto* lane = manager.getLane(laneId);
        if (lane == nullptr || lane->type != expectedType)
            return false;
        preparedState.captured = true;
        preparedState.laneExisted = true;
        preparedState.lane = *lane;
        preparedState.lane.authorityState =
            automationAuthorityForPersistence(preparedState.lane.authorityState);
        preparedState.clips = clipsForLane(manager, *lane);
    }

    const auto* lane = manager.getLane(laneId);
    return lane != nullptr && lane->type == expectedType;
}

void activateBakedLane(AutomationLaneId laneId) {
    auto& manager = AutomationManager::getInstance();
    const auto* lane = manager.getLane(laneId);
    if (lane == nullptr)
        return;

    const auto target = lane->target;
    manager.setTargetUserTouched(target, false);
    manager.endTargetGesture(target);
    manager.clearTouchBaseline(target);
    manager.setLaneEnabled(laneId, true);
    // setLaneEnabled is intentionally a no-op when already Reading. Still
    // notify controls after clearing stale gesture bookkeeping.
    manager.invalidateLane(laneId);
}

/** Flip off the given links; returns the ones that were actually enabled. */
std::vector<BakeModulationCommand::ModLinkRef> disableModLinks(
    const std::vector<BakeModulationCommand::ModLinkRef>& refs) {
    auto& trackMgr = TrackManager::getInstance();
    const auto& constTrackMgr = trackMgr;
    std::vector<BakeModulationCommand::ModLinkRef> disabled;
    for (const auto& ref : refs) {
        const auto node = constTrackMgr.resolveChainNode(ref.path);
        if (!node.valid() || ref.modIndex < 0 ||
            ref.modIndex >= static_cast<int>(node.mods->size()))
            continue;
        const auto* link = (*node.mods)[static_cast<size_t>(ref.modIndex)].getLink(ref.target);
        if (link == nullptr || !link->enabled)
            continue;
        trackMgr.setModLinkEnabled(ref.path, ref.modIndex, ref.target, false);
        disabled.push_back(ref);
    }
    return disabled;
}

void enableModLinks(const std::vector<BakeModulationCommand::ModLinkRef>& refs) {
    auto& trackMgr = TrackManager::getInstance();
    for (const auto& ref : refs)
        trackMgr.setModLinkEnabled(ref.path, ref.modIndex, ref.target, true);
}

}  // namespace

BakeAutomationLaneState captureBakeAutomationLaneState(const AutomationTarget& target) {
    BakeAutomationLaneState state;
    state.captured = true;

    const auto& manager = AutomationManager::getInstance();
    const auto laneId = manager.getLaneForTarget(target);
    const auto* lane = manager.getLane(laneId);
    if (lane == nullptr)
        return state;

    state.laneExisted = true;
    state.lane = *lane;
    // A popup-menu command must never restore a half-finished mouse gesture.
    state.lane.authorityState = automationAuthorityForPersistence(state.lane.authorityState);
    state.clips = clipsForLane(manager, *lane);
    return state;
}

void restoreBakeAutomationLaneState(AutomationLaneId preparedLaneId,
                                    const BakeAutomationLaneState& state) {
    if (!state.captured)
        return;
    if (state.laneExisted)
        restoreBakeLaneState(state);
    else
        AutomationManager::getInstance().deleteLane(preparedLaneId);
}

void BakeModulationCommand::execute() {
    auto& autoMgr = AutomationManager::getInstance();
    if (!prepareBakeLaneForExecute(laneId_, AutomationLaneType::Absolute, previousLaneState_,
                                   preparedLaneState_))
        return;

    const auto* lane = autoMgr.getLane(laneId_);

    if (!capturedLaneAuthority_) {
        previousLaneDisabled_ = isAutomationPersistentlyDisabled(lane->authorityState);
        capturedLaneAuthority_ = true;
    }
    // Baking is a terminal action for the current modulation-linking gesture.
    // Keeping the transient mode alive leaves its banner and overlays active
    // even though the source link is about to be disabled.
    LinkModeManager::getInstance().exitAllLinkModes();
    removedPoints_ = autoMgr.replacePointsInRange(laneId_, startBeat_, endBeat_, bakedPoints_);
    disabledLinks_ = disableModLinks(linksToDisable_);
    // A bake replaces the source modulation with automation, so the resulting
    // lane must enter Reading even if it was explicitly disabled beforehand.
    activateBakedLane(laneId_);
}

void BakeModulationCommand::undo() {
    if (previousLaneState_.captured) {
        restoreBakeAutomationLaneState(laneId_, previousLaneState_);
    } else {
        AutomationManager::getInstance().replacePointsInRange(laneId_, startBeat_, endBeat_,
                                                              removedPoints_);
        if (capturedLaneAuthority_)
            AutomationManager::getInstance().setLaneEnabled(laneId_, !previousLaneDisabled_);
    }
    enableModLinks(disabledLinks_);
    disabledLinks_.clear();
}

void BakeModulationToClipCommand::execute() {
    auto& autoMgr = AutomationManager::getInstance();
    if (endBeat_ <= startBeat_ ||
        !prepareBakeLaneForExecute(laneId_, AutomationLaneType::ClipBased, previousLaneState_,
                                   preparedLaneState_))
        return;

    const auto* lane = autoMgr.getLane(laneId_);

    if (!capturedLaneAuthority_) {
        previousLaneDisabled_ = isAutomationPersistentlyDisabled(lane->authorityState);
        capturedLaneAuthority_ = true;
    }

    createdClipId_ = autoMgr.createClip(laneId_, startBeat_, endBeat_ - startBeat_);
    if (createdClipId_ == INVALID_AUTOMATION_CLIP_ID) {
        restoreBakeAutomationLaneState(laneId_, previousLaneState_);
        return;
    }

    LinkModeManager::getInstance().exitAllLinkModes();
    auto localPoints = bakedPoints_;
    for (auto& point : localPoints)
        point.beatPosition -= startBeat_;
    autoMgr.setClipPoints(createdClipId_, std::move(localPoints));
    if (loopLengthBeats_ > 0.0) {
        autoMgr.setClipLooping(createdClipId_, true);
        autoMgr.setClipLoopLength(createdClipId_, loopLengthBeats_);
    }
    autoMgr.moveClipToFront(createdClipId_);

    disabledLinks_ = disableModLinks(linksToDisable_);
    activateBakedLane(laneId_);
}

void BakeModulationToClipCommand::undo() {
    if (previousLaneState_.captured) {
        restoreBakeAutomationLaneState(laneId_, previousLaneState_);
        createdClipId_ = INVALID_AUTOMATION_CLIP_ID;
    } else if (createdClipId_ != INVALID_AUTOMATION_CLIP_ID) {
        AutomationManager::getInstance().deleteClip(createdClipId_);
        createdClipId_ = INVALID_AUTOMATION_CLIP_ID;
        if (capturedLaneAuthority_)
            AutomationManager::getInstance().setLaneEnabled(laneId_, !previousLaneDisabled_);
    }
    enableModLinks(disabledLinks_);
    disabledLinks_.clear();
}

bool DuplicateAutomationTimeSelectionCommand::canDuplicatePoints() const {
    if (endBeat_ <= startBeat_)
        return false;

    const auto& mgr = AutomationManager::getInstance();
    for (const auto& lane : mgr.getLanes()) {
        if (!shouldDuplicateLane(lane))
            continue;

        for (const auto& point : lane.absolutePoints) {
            if (pointIsInDuplicateRange(point.beatPosition, startBeat_, endBeat_))
                return true;
        }
    }
    return false;
}

void DuplicateAutomationTimeSelectionCommand::execute() {
    insertedPoints_.clear();

    const double duration = endBeat_ - startBeat_;
    if (duration <= 0.0)
        return;

    auto& mgr = AutomationManager::getInstance();
    std::vector<std::pair<AutomationLaneId, AutomationPoint>> pointsToDuplicate;

    for (const auto& lane : mgr.getLanes()) {
        if (!shouldDuplicateLane(lane))
            continue;

        for (const auto& point : lane.absolutePoints) {
            if (pointIsInDuplicateRange(point.beatPosition, startBeat_, endBeat_)) {
                pointsToDuplicate.emplace_back(lane.id, point);
            }
        }
    }

    if (pointsToDuplicate.empty())
        return;

    AutomationManager::BatchScope batch;
    insertedPoints_.reserve(pointsToDuplicate.size());

    for (auto [laneId, point] : pointsToDuplicate) {
        const double newBeat = destinationStartBeat_ + (point.beatPosition - startBeat_);
        const auto newId = mgr.addPoint(laneId, newBeat, point.value, point.curveType);
        if (newId == INVALID_AUTOMATION_POINT_ID)
            continue;

        mgr.setPointTension(laneId, newId, point.tension);
        mgr.setPointHandles(laneId, newId, point.inHandle, point.outHandle);
        insertedPoints_.push_back({laneId, newId});
    }
}

void DuplicateAutomationTimeSelectionCommand::undo() {
    if (insertedPoints_.empty())
        return;

    auto& mgr = AutomationManager::getInstance();
    AutomationManager::BatchScope batch;
    for (auto& insertedPoint : std::views::reverse(insertedPoints_)) {
        mgr.deletePoint(insertedPoint.laneId, insertedPoint.pointId);
    }
    insertedPoints_.clear();
}

// ============================================================================
// InsertTimeAutomationCommand
// ============================================================================

bool InsertTimeAutomationCommand::shouldShiftLane(const AutomationLaneInfo& lane) const {
    if (!lane.visible || !lane.isAbsolute())
        return false;
    // The Tempo lane mirrors edit.tempoSequence, which is rippled directly by
    // TempoSequenceRippleCommand. Shifting it here too would double the ripple.
    if (lane.target.kind == ControlTarget::Kind::Tempo)
        return false;
    if (!laneIds_.empty()) {
        return std::find(laneIds_.begin(), laneIds_.end(), lane.id) != laneIds_.end();
    }
    if (trackIds_.empty())
        return true;
    return std::find(trackIds_.begin(), trackIds_.end(), lane.target.devicePath.trackId) !=
           trackIds_.end();
}

bool InsertTimeAutomationCommand::canShiftPoints() const {
    if (durationBeats_ <= 0.0)
        return false;

    constexpr double epsilon = 1.0e-9;
    const auto& mgr = AutomationManager::getInstance();
    for (const auto& lane : mgr.getLanes()) {
        if (!shouldShiftLane(lane))
            continue;
        for (const auto& point : lane.absolutePoints) {
            if (point.beatPosition >= insertBeat_ - epsilon)
                return true;
        }
    }
    return false;
}

void InsertTimeAutomationCommand::execute() {
    shiftedPoints_.clear();

    if (durationBeats_ <= 0.0)
        return;

    constexpr double epsilon = 1.0e-9;
    auto& mgr = AutomationManager::getInstance();

    for (const auto& lane : mgr.getLanes()) {
        if (!shouldShiftLane(lane))
            continue;
        for (const auto& point : lane.absolutePoints) {
            if (point.beatPosition >= insertBeat_ - epsilon)
                shiftedPoints_.push_back({lane.id, point.id, point.beatPosition, point.value});
        }
    }

    if (shiftedPoints_.empty())
        return;

    // Move the rightmost points first so each destination beat is always empty
    // (every shifted point moves right by the same amount).
    std::sort(shiftedPoints_.begin(), shiftedPoints_.end(),
              [](const ShiftedPoint& a, const ShiftedPoint& b) { return a.oldBeat > b.oldBeat; });

    AutomationManager::BatchScope batch;
    for (const auto& sp : shiftedPoints_) {
        mgr.movePoint(sp.laneId, sp.pointId, sp.oldBeat + durationBeats_, sp.value);
    }
}

void InsertTimeAutomationCommand::undo() {
    if (shiftedPoints_.empty())
        return;

    auto& mgr = AutomationManager::getInstance();
    AutomationManager::BatchScope batch;
    // Move leftmost first so each original beat is empty as we restore it.
    for (auto& shiftedPoint : std::views::reverse(shiftedPoints_)) {
        mgr.movePoint(shiftedPoint.laneId, shiftedPoint.pointId, shiftedPoint.oldBeat,
                      shiftedPoint.value);
    }
    shiftedPoints_.clear();
}

// ============================================================================
// SetAutomationLanePointsCommand
// ============================================================================

SetAutomationLanePointsCommand::SetAutomationLanePointsCommand(AutomationLaneId laneId,
                                                               std::vector<AutomationPoint> points)
    : laneId_(laneId), points_(std::move(points)) {
    auto& mgr = AutomationManager::getInstance();
    const auto* lane = mgr.getLane(laneId_);
    if (lane == nullptr || !lane->isAbsolute())
        return;

    storedLane_ = *lane;
    for (auto clipId : lane->clipIds) {
        if (const auto* clip = mgr.getClip(clipId))
            storedClips_.push_back(*clip);
    }
    captured_ = true;
}

void SetAutomationLanePointsCommand::execute() {
    if (!captured_)
        return;

    auto& mgr = AutomationManager::getInstance();
    auto* lane = mgr.getLane(laneId_);
    if (lane == nullptr)
        return;

    // One batch so listeners see the replacement as a single change rather than
    // a clear followed by N inserts.
    AutomationManager::BatchScope batch;
    mgr.clearLanePoints(laneId_);
    for (const auto& point : points_)
        mgr.addPoint(laneId_, point.beatPosition, point.value, point.curveType);

    applied_ = true;
}

void SetAutomationLanePointsCommand::undo() {
    if (!applied_)
        return;
    AutomationManager::getInstance().restoreLaneState(storedLane_, storedClips_);
}

}  // namespace magda
