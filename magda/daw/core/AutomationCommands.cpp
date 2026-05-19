#include "AutomationCommands.hpp"

#include <algorithm>

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
    if (!laneIds_.empty()) {
        return std::find(laneIds_.begin(), laneIds_.end(), lane.id) != laneIds_.end();
    }
    if (trackIds_.empty())
        return true;
    return std::find(trackIds_.begin(), trackIds_.end(), lane.target.devicePath.trackId) !=
           trackIds_.end();
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
    for (auto it = insertedPoints_.rbegin(); it != insertedPoints_.rend(); ++it) {
        mgr.deletePoint(it->laneId, it->pointId);
    }
    insertedPoints_.clear();
}

}  // namespace magda
