#include "AutomationCurveEditor.hpp"

#include <algorithm>
#include <cmath>

#include "core/AutomationCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

AutomationCurveEditor::AutomationCurveEditor(AutomationLaneId laneId) : laneId_(laneId) {
    setName("AutomationCurveEditor");

    // Wire up base class snapping to automation snapping
    CurveEditorBase::snapXToGrid = [this](double x) -> double {
        if (snapTimeToGrid) {
            return snapTimeToGrid(x);
        }
        return x;
    };

    // Register listeners
    AutomationManager::getInstance().addListener(this);
    SelectionManager::getInstance().addListener(this);

    rebuildPointComponents();
}

AutomationCurveEditor::~AutomationCurveEditor() {
    AutomationManager::getInstance().removeListener(this);
    SelectionManager::getInstance().removeListener(this);
}

// AutomationManagerListener
void AutomationCurveEditor::automationLanesChanged() {
    pointsCacheDirty_ = true;
    rebuildPointComponents();
}

void AutomationCurveEditor::automationLanePropertyChanged(AutomationLaneId laneId) {
    if (laneId == laneId_) {
        repaint();
    }
}

void AutomationCurveEditor::automationPointsChanged(AutomationLaneId laneId) {
    if (laneId == laneId_) {
        // Clear preview when points are committed
        previewPointId_ = INVALID_CURVE_POINT_ID;
        pointsCacheDirty_ = true;
        rebuildPointComponents();
    }
}

void AutomationCurveEditor::automationPointDragPreview(AutomationLaneId laneId,
                                                       AutomationPointId pointId,
                                                       double previewTime, double previewValue) {
    if (laneId != laneId_)
        return;

    previewPointId_ = pointId;
    previewX_ = previewTime;
    previewY_ = previewValue;

    // Update the point component position for visual feedback
    for (auto& pc : pointComponents_) {
        if (pc->getPointId() == pointId) {
            int x = xToPixel(previewTime);
            int y = yToPixel(previewValue);
            pc->setCentrePosition(x, y);
            break;
        }
    }

    repaint();
}

// SelectionManagerListener
void AutomationCurveEditor::selectionTypeChanged(SelectionType newType) {
    juce::ignoreUnused(newType);
    syncSelectionState();
}

void AutomationCurveEditor::automationPointSelectionChanged(
    const AutomationPointSelection& selection) {
    juce::ignoreUnused(selection);
    syncSelectionState();
}

void AutomationCurveEditor::setLaneId(AutomationLaneId laneId) {
    if (laneId_ != laneId) {
        laneId_ = laneId;
        pointsCacheDirty_ = true;
        rebuildPointComponents();
    }
}

void AutomationCurveEditor::setDrawMode(AutomationDrawMode mode) {
    CurveDrawMode curveMode;
    switch (mode) {
        case AutomationDrawMode::Select:
            curveMode = CurveDrawMode::Select;
            break;
        case AutomationDrawMode::Pencil:
            curveMode = CurveDrawMode::Pencil;
            break;
        case AutomationDrawMode::Line:
            curveMode = CurveDrawMode::Line;
            break;
        case AutomationDrawMode::Curve:
            curveMode = CurveDrawMode::Curve;
            break;
    }
    CurveEditorBase::setDrawMode(curveMode);
}

AutomationDrawMode AutomationCurveEditor::getAutomationDrawMode() const {
    switch (CurveEditorBase::getDrawMode()) {
        case CurveDrawMode::Select:
            return AutomationDrawMode::Select;
        case CurveDrawMode::Pencil:
            return AutomationDrawMode::Pencil;
        case CurveDrawMode::Line:
            return AutomationDrawMode::Line;
        case CurveDrawMode::Curve:
            return AutomationDrawMode::Curve;
    }
    return AutomationDrawMode::Select;
}

double AutomationCurveEditor::pixelToX(int px) const {
    return (px / pixelsPerSecond_) + clipOffset_;
}

int AutomationCurveEditor::xToPixel(double x) const {
    return static_cast<int>((x - clipOffset_) * pixelsPerSecond_);
}

const std::vector<CurvePoint>& AutomationCurveEditor::getPoints() const {
    if (pointsCacheDirty_) {
        updatePointsCache();
    }
    return cachedPoints_;
}

void AutomationCurveEditor::updatePointsCache() const {
    cachedPoints_.clear();

    const std::vector<AutomationPoint>* sourcePoints = nullptr;

    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        const auto* clip = AutomationManager::getInstance().getClip(clipId_);
        if (clip) {
            sourcePoints = &clip->points;
        }
    } else {
        const auto* lane = AutomationManager::getInstance().getLane(laneId_);
        if (lane && lane->isAbsolute()) {
            sourcePoints = &lane->absolutePoints;
        }
    }

    if (sourcePoints) {
        cachedPoints_.reserve(sourcePoints->size());
        for (const auto& ap : *sourcePoints) {
            CurvePoint cp;
            cp.id = ap.id;
            cp.x = ap.time;
            cp.y = ap.value;
            cp.curveType = toCurveType(ap.curveType);
            cp.tension = ap.tension;
            cp.inHandle.x = ap.inHandle.time;
            cp.inHandle.y = ap.inHandle.value;
            cp.inHandle.linked = ap.inHandle.linked;
            cp.outHandle.x = ap.outHandle.time;
            cp.outHandle.y = ap.outHandle.value;
            cp.outHandle.linked = ap.outHandle.linked;
            cachedPoints_.push_back(cp);
        }
    }

    pointsCacheDirty_ = false;
}

void AutomationCurveEditor::onPointAdded(double x, double y, CurveType curveType) {
    AutomationCurveType autoCurveType = toAutomationCurveType(curveType);

    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(std::make_unique<AddAutomationPointCommand>(
            laneId_, clipId_, x - clipOffset_, y, autoCurveType));
    } else {
        UndoManager::getInstance().executeCommand(std::make_unique<AddAutomationPointCommand>(
            laneId_, INVALID_AUTOMATION_CLIP_ID, x, y, autoCurveType));
    }
}

void AutomationCurveEditor::onPointMoved(uint32_t pointId, double newX, double newY) {
    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(std::make_unique<MoveAutomationPointCommand>(
            laneId_, clipId_, static_cast<AutomationPointId>(pointId), newX - clipOffset_, newY));
    } else {
        UndoManager::getInstance().executeCommand(std::make_unique<MoveAutomationPointCommand>(
            laneId_, INVALID_AUTOMATION_CLIP_ID, static_cast<AutomationPointId>(pointId), newX,
            newY));
    }
}

void AutomationCurveEditor::onPointDeleted(uint32_t pointId) {
    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(std::make_unique<DeleteAutomationPointCommand>(
            laneId_, clipId_, static_cast<AutomationPointId>(pointId)));
    } else {
        UndoManager::getInstance().executeCommand(std::make_unique<DeleteAutomationPointCommand>(
            laneId_, INVALID_AUTOMATION_CLIP_ID, static_cast<AutomationPointId>(pointId)));
    }
}

void AutomationCurveEditor::onPointSelected(uint32_t pointId) {
    SelectionManager::getInstance().selectAutomationPoint(laneId_, pointId, clipId_);
}

void AutomationCurveEditor::onTensionChanged(uint32_t pointId, double tension) {
    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetAutomationPointTensionCommand>(
                laneId_, clipId_, static_cast<AutomationPointId>(pointId), tension));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetAutomationPointTensionCommand>(
                laneId_, INVALID_AUTOMATION_CLIP_ID, static_cast<AutomationPointId>(pointId),
                tension));
    }
}

void AutomationCurveEditor::onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                                             const CurveHandleData& outHandle) {
    // Convert CurveHandleData to BezierHandle
    BezierHandle inH;
    inH.time = inHandle.x;
    inH.value = inHandle.y;
    inH.linked = inHandle.linked;

    BezierHandle outH;
    outH.time = outHandle.x;
    outH.value = outHandle.y;
    outH.linked = outHandle.linked;

    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetAutomationPointHandlesCommand>(
                laneId_, clipId_, static_cast<AutomationPointId>(pointId), inH, outH));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetAutomationPointHandlesCommand>(
                laneId_, INVALID_AUTOMATION_CLIP_ID, static_cast<AutomationPointId>(pointId), inH,
                outH));
    }
}

void AutomationCurveEditor::syncSelectionState() {
    auto& selectionManager = SelectionManager::getInstance();
    const auto& selection = selectionManager.getAutomationPointSelection();

    bool isOurSelection = selectionManager.getSelectionType() == SelectionType::AutomationPoint &&
                          selection.laneId == laneId_ &&
                          (clipId_ == INVALID_AUTOMATION_CLIP_ID || selection.clipId == clipId_);

    for (auto& pc : pointComponents_) {
        bool isSelected = false;
        if (isOurSelection) {
            isSelected = std::find(selection.pointIds.begin(), selection.pointIds.end(),
                                   pc->getPointId()) != selection.pointIds.end();
        }
        pc->setSelected(isSelected);
    }

    repaint();
}

void AutomationCurveEditor::rebuildPointComponents() {
    // Update cache before rebuilding
    pointsCacheDirty_ = true;
    updatePointsCache();

    // Call base class implementation
    CurveEditorBase::rebuildPointComponents();
}

void AutomationCurveEditor::deleteSelectedPoints() {
    auto& selectionManager = SelectionManager::getInstance();
    if (!selectionManager.hasAutomationPointSelection())
        return;

    const auto& selection = selectionManager.getAutomationPointSelection();
    if (selection.laneId != laneId_)
        return;

    // Delete in reverse order to maintain indices, grouped as one undo step
    CompoundOperationScope scope("Delete Automation Points");
    auto pointIds = selection.pointIds;
    for (auto it = pointIds.rbegin(); it != pointIds.rend(); ++it) {
        if (selection.clipId != INVALID_AUTOMATION_CLIP_ID) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<DeleteAutomationPointCommand>(
                    laneId_, selection.clipId, static_cast<AutomationPointId>(*it)));
        } else {
            UndoManager::getInstance().executeCommand(
                std::make_unique<DeleteAutomationPointCommand>(
                    laneId_, INVALID_AUTOMATION_CLIP_ID, static_cast<AutomationPointId>(*it)));
        }
    }

    selectionManager.clearAutomationPointSelection();
}

CurveType AutomationCurveEditor::toCurveType(AutomationCurveType type) {
    switch (type) {
        case AutomationCurveType::Linear:
            return CurveType::Linear;
        case AutomationCurveType::Bezier:
            return CurveType::Bezier;
        case AutomationCurveType::Step:
            return CurveType::Step;
    }
    return CurveType::Linear;
}

AutomationCurveType AutomationCurveEditor::toAutomationCurveType(CurveType type) {
    switch (type) {
        case CurveType::Linear:
            return AutomationCurveType::Linear;
        case CurveType::Bezier:
            return AutomationCurveType::Bezier;
        case CurveType::Step:
            return AutomationCurveType::Step;
    }
    return AutomationCurveType::Linear;
}

}  // namespace magda
