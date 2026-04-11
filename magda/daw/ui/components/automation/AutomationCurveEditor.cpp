#include "AutomationCurveEditor.hpp"

#include <algorithm>
#include <cmath>

#include "core/AutomationCommands.hpp"
#include "core/ParameterUtils.hpp"
#include "core/UndoManager.hpp"

namespace magda {

AutomationCurveEditor::AutomationCurveEditor(AutomationLaneId laneId) : laneId_(laneId) {
    setName("AutomationCurveEditor");

    // Wire up base class snapping to automation snapping.
    // Gated on the lane's snapTime flag so the user can disable per-lane.
    CurveEditorBase::snapXToGrid = [this](double x) -> double {
        const auto* lane = AutomationManager::getInstance().getLane(laneId_);
        if (lane && !lane->snapTime)
            return x;
        if (snapTimeToGrid) {
            return snapTimeToGrid(x);
        }
        return x;
    };

    // Y snap: delegate to ParameterUtils using the lane's parameter info.
    // Wired via the base class so drag-previews jump live instead of
    // only snapping on release.
    CurveEditorBase::snapYToGrid = [this](double y) -> double { return applyValueSnap(y); };

    CurveEditorBase::getGridSpacingX = [this]() -> double {
        if (getGridSpacingBeats) {
            return getGridSpacingBeats();
        }
        return 1.0;  // Default: 1 beat
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
        if (pc->getPointId() == static_cast<uint32_t>(pointId)) {
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

void AutomationCurveEditor::setPixelsPerBeat(double ppb) {
    if (std::abs(ppb - pixelsPerBeat_) < 0.001)
        return;
    pixelsPerBeat_ = ppb;
    updatePointPositions();
    repaint();
}

double AutomationCurveEditor::pixelToX(int px) const {
    return (static_cast<double>(px) / pixelsPerBeat_) + clipOffset_;
}

int AutomationCurveEditor::xToPixel(double x) const {
    return static_cast<int>(std::round((x - clipOffset_) * pixelsPerBeat_));
}

void AutomationCurveEditor::paintGrid(juce::Graphics& g) {
    const auto* lane = AutomationManager::getInstance().getLane(laneId_);
    if (!lane) {
        CurveEditorBase::paintGrid(g);
        return;
    }

    auto paramInfo = lane->target.getParameterInfo();

    // Build list of normalized grid positions based on parameter type
    std::vector<double> gridNorms;

    if (paramInfo.scale == ParameterScale::FaderDB) {
        // dB values that make sense for a fader
        const double dbValues[] = {6.0, 3.0, 0.0, -6.0, -12.0, -18.0, -24.0, -36.0, -48.0, -60.0};
        for (double db : dbValues) {
            float norm = ParameterUtils::realToNormalized(static_cast<float>(db), paramInfo);
            gridNorms.push_back(static_cast<double>(norm));
        }
    } else if (lane->target.type == AutomationTargetType::TrackPan) {
        // Pan: fine divisions from -1 to +1
        for (double pan = -1.0; pan <= 1.0; pan += 0.25) {
            float norm = ParameterUtils::realToNormalized(static_cast<float>(pan), paramInfo);
            gridNorms.push_back(static_cast<double>(norm));
        }
    } else if (paramInfo.isBipolar()) {
        // Symmetric real-value grid so 0 lands exactly at mid-lane.
        // Quarter + half divisions give a readable ±max, ±50%, 0 grid.
        float absMax = std::max(std::abs(paramInfo.minValue), std::abs(paramInfo.maxValue));
        const double frac[] = {-1.0, -0.5, 0.0, 0.5, 1.0};
        for (double f : frac) {
            float norm =
                ParameterUtils::realToNormalized(static_cast<float>(f * absMax), paramInfo);
            gridNorms.push_back(static_cast<double>(norm));
        }
    } else {
        // Generic: 10% increments
        for (int i = 1; i < 10; ++i) {
            gridNorms.push_back(i / 10.0);
        }
    }

    auto bounds = getLocalBounds();
    float width = static_cast<float>(bounds.getWidth());

    // For bipolar parameters the neutral-value line (0 dB, 0 st, …) should
    // read as the rest position. Draw it noticeably brighter than the rest
    // of the grid.
    double zeroNorm = -1.0;
    if (paramInfo.isBipolar()) {
        zeroNorm = static_cast<double>(ParameterUtils::realToNormalized(0.0f, paramInfo));
    }

    for (double norm : gridNorms) {
        if (norm <= 0.01 || norm >= 0.99)
            continue;
        int y = yToPixel(norm);
        bool isZeroLine = zeroNorm >= 0.0 && std::abs(norm - zeroNorm) < 0.002;
        g.setColour(isZeroLine ? juce::Colour(0x50FFFFFF) : juce::Colour(0x18FFFFFF));
        g.drawHorizontalLine(y, 0.0f, width);
    }
}

juce::String AutomationCurveEditor::formatValueLabel(double y) const {
    const auto* lane = AutomationManager::getInstance().getLane(laneId_);
    if (!lane)
        return CurveEditorBase::formatValueLabel(y);

    auto paramInfo = lane->target.getParameterInfo();
    float realValue = ParameterUtils::normalizedToReal(static_cast<float>(y), paramInfo);
    return ParameterUtils::formatValue(realValue, paramInfo);
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
    y = applyValueSnap(y);

    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(std::make_unique<AddAutomationPointCommand>(
            laneId_, clipId_, x - clipOffset_, y, autoCurveType));
    } else {
        UndoManager::getInstance().executeCommand(std::make_unique<AddAutomationPointCommand>(
            laneId_, INVALID_AUTOMATION_CLIP_ID, x, y, autoCurveType));
    }
}

void AutomationCurveEditor::onPointDragPreview(uint32_t pointId, double newX, double newY) {
    // Broadcast the in-progress drag so AutomationPlaybackEngine can push the
    // preview value straight into the TE parameter — this keeps the fader /
    // knob tracking the drag in real time without waiting for the mouseUp
    // commit + full rebake. Visual point movement is already handled by the
    // base-class lambda; this notification is purely for audio-side listeners.
    AutomationManager::getInstance().notifyPointDragPreview(
        laneId_, static_cast<AutomationPointId>(pointId), newX, newY);
}

void AutomationCurveEditor::onPointMoved(uint32_t pointId, double newX, double newY) {
    // Value snap is already applied by the base class lambda wrapper
    // before this virtual is invoked, so we don't re-snap here.

    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        UndoManager::getInstance().executeCommand(std::make_unique<MoveAutomationPointCommand>(
            laneId_, clipId_, static_cast<AutomationPointId>(pointId), newX - clipOffset_, newY));
    } else {
        UndoManager::getInstance().executeCommand(std::make_unique<MoveAutomationPointCommand>(
            laneId_, INVALID_AUTOMATION_CLIP_ID, static_cast<AutomationPointId>(pointId), newX,
            newY));
    }
}

double AutomationCurveEditor::applyValueSnap(double normalized) const {
    const auto* lane = AutomationManager::getInstance().getLane(laneId_);
    if (!lane || !lane->snapValue)
        return normalized;
    return ParameterUtils::snapNormalizedToGrid(normalized, lane->target.getParameterInfo());
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

void AutomationCurveEditor::onStepStamped(double gridStart, double gridEnd, double y,
                                          uint32_t prevPointId, double prevValue) {
    // Serum-style step stamp: make the grid cell a dip back to the
    // previous value. Left edge cliffs from prevValue down/up to y,
    // cell holds at y, right edge cliffs back to prevValue.
    //
    // A segment's shape is controlled by the point BEFORE it, so to get
    // the left-edge cliff we flip the preceding point's curveType to
    // Step as part of the same undo step. The recovery point inherits
    // the *original* curveType of the preceding point so the segment
    // flowing out of the cell toward whatever existing point comes next
    // keeps its original shape (linear fades stay as linear fades, etc).
    CompoundOperationScope scope("Stamp Automation Step");

    AutomationCurveType originalPrevType = AutomationCurveType::Linear;
    bool prevFound = false;
    bool nextExistsAtGridEnd = false;

    auto gatherContext = [&](const std::vector<AutomationPoint>& points) {
        constexpr double kTimeEps = 1e-6;
        for (const auto& p : points) {
            if (p.id == static_cast<AutomationPointId>(prevPointId)) {
                originalPrevType = p.curveType;
                prevFound = true;
            }
            if (std::abs(p.time - gridEnd) < kTimeEps)
                nextExistsAtGridEnd = true;
        }
    };

    if (clipId_ != INVALID_AUTOMATION_CLIP_ID) {
        if (const auto* clip = AutomationManager::getInstance().getClip(clipId_))
            gatherContext(clip->points);
    } else if (const auto* lane = AutomationManager::getInstance().getLane(laneId_)) {
        gatherContext(lane->absolutePoints);
    }

    if (prevPointId != INVALID_CURVE_POINT_ID && prevFound &&
        originalPrevType != AutomationCurveType::Step) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetAutomationPointCurveTypeCommand>(
                laneId_, clipId_, static_cast<AutomationPointId>(prevPointId),
                AutomationCurveType::Step));
    }

    // Add the cell's left edge at the click value.
    onPointAdded(gridStart, y, CurveType::Step);

    // Recovery point at gridEnd — only if we have a baseline to return
    // to AND there isn't already a point there. Uses the original prev
    // curveType so downstream interpolation is preserved.
    if (prevPointId != INVALID_CURVE_POINT_ID && gridEnd > gridStart && !nextExistsAtGridEnd) {
        CurveType recoveryType = toCurveType(originalPrevType);
        onPointAdded(gridEnd, prevValue, recoveryType);
    }
}

void AutomationCurveEditor::onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) {
    if (pointIds.empty())
        return;

    CompoundOperationScope scope("Delete Automation Points");
    for (auto it = pointIds.rbegin(); it != pointIds.rend(); ++it) {
        onPointDeleted(*it);
    }
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
