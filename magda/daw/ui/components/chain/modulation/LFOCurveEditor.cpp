#include "modulation/LFOCurveEditor.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {

bool sameCurvePoints(const std::vector<CurvePointData>& a, const std::vector<CurvePointData>& b) {
    if (a.size() != b.size())
        return false;

    constexpr float epsilon = 1.0e-6f;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& lhs = a[i];
        const auto& rhs = b[i];
        if (std::abs(lhs.phase - rhs.phase) > epsilon ||
            std::abs(lhs.value - rhs.value) > epsilon ||
            std::abs(lhs.tension - rhs.tension) > epsilon || lhs.curveType != rhs.curveType ||
            std::abs(lhs.inHandleX - rhs.inHandleX) > epsilon ||
            std::abs(lhs.inHandleY - rhs.inHandleY) > epsilon ||
            std::abs(lhs.outHandleX - rhs.outHandleX) > epsilon ||
            std::abs(lhs.outHandleY - rhs.outHandleY) > epsilon) {
            return false;
        }
    }
    return true;
}

class SetLFOCurveStateCommand : public UndoableCommand {
  public:
    SetLFOCurveStateCommand(ChainNodePath ownerPath, int modIndex, CurvePreset beforePreset,
                            std::vector<CurvePointData> beforePoints, CurvePreset afterPreset,
                            std::vector<CurvePointData> afterPoints, juce::String description)
        : ownerPath_(std::move(ownerPath)),
          modIndex_(modIndex),
          beforePreset_(beforePreset),
          beforePoints_(std::move(beforePoints)),
          afterPreset_(afterPreset),
          afterPoints_(std::move(afterPoints)),
          description_(std::move(description)) {}

    void execute() override {
        TrackManager::getInstance().setModCurveState(ownerPath_, modIndex_, afterPreset_,
                                                     afterPoints_);
    }

    void undo() override {
        TrackManager::getInstance().setModCurveState(ownerPath_, modIndex_, beforePreset_,
                                                     beforePoints_);
    }

    juce::String getDescription() const override {
        return description_;
    }

  private:
    ChainNodePath ownerPath_;
    int modIndex_ = -1;
    CurvePreset beforePreset_ = CurvePreset::Custom;
    std::vector<CurvePointData> beforePoints_;
    CurvePreset afterPreset_ = CurvePreset::Custom;
    std::vector<CurvePointData> afterPoints_;
    juce::String description_;
};

juce::String formatCurvePointTypes(const std::vector<CurvePoint>& points) {
    juce::String result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0)
            result += ",";
        result += juce::String(static_cast<int>(points[i].id));
        result += ":";
        result += juce::String(curveTypeToInt(points[i].curveType));
    }
    return result;
}

juce::String formatModCurvePointTypes(const std::vector<CurvePointData>& points) {
    juce::String result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0)
            result += ",";
        result += juce::String(static_cast<int>(i));
        result += ":";
        result += juce::String(points[i].curveType);
    }
    return result;
}

}  // namespace

LFOCurveEditor::LFOCurveEditor() {
    setName("LFOCurveEditor");

    // Padding allows edge dots to extend beyond content area without clipping.
    // Must be >= half of POINT_SIZE_SELECTED (8) so extreme points are fully grabbable.
    setPadding(8);

    // Snap callbacks used by the base for adding points and dragging segment
    // shaper handles (point drags also run through constrainPointPosition).
    // Without these, only point drags snapped — new points and the hard-corner
    // apex ignored the grid.
    snapXToGrid = [this](double x) -> double {
        if (snapX_ && gridDivisionsX_ > 1) {
            const double step = 1.0 / gridDivisionsX_;
            return juce::jlimit(0.0, 1.0, std::round(x / step) * step);
        }
        return x;
    };
    snapYToGrid = [this](double y) -> double {
        if (snapY_ && gridDivisionsY_ > 1) {
            const double step = 1.0 / gridDivisionsY_;
            return juce::jlimit(0.0, 1.0, std::round(y / step) * step);
        }
        return y;
    };
    // Step-stamp cell width = one X grid division (phase is 0-1). Without this
    // the base skips the snap/size block and the step cell collapses.
    getGridSpacingX = [this]() -> double {
        return gridDivisionsX_ > 0 ? 1.0 / gridDivisionsX_ : 0.0;
    };

    rebuildPointComponents();
    startTimer(33);  // 30 FPS animation for phase indicator
}

LFOCurveEditor::~LFOCurveEditor() {
    stopTimer();
}

void LFOCurveEditor::setUndoTarget(const ChainNodePath& ownerPath, int modIndex) {
    undoOwnerPath_ = ownerPath;
    undoModIndex_ = modIndex;
}

std::vector<CurvePointData> LFOCurveEditor::snapshotCurvePoints() const {
    std::vector<CurvePointData> points;
    points.reserve(points_.size());

    for (const auto& p : points_) {
        CurvePointData cpd;
        cpd.phase = static_cast<float>(p.x);
        cpd.value = static_cast<float>(p.y);
        cpd.tension = static_cast<float>(p.tension);
        cpd.curveType = curveTypeToInt(p.curveType);
        cpd.inHandleX = static_cast<float>(p.inHandle.x);
        cpd.inHandleY = static_cast<float>(p.inHandle.y);
        cpd.outHandleX = static_cast<float>(p.outHandle.x);
        cpd.outHandleY = static_cast<float>(p.outHandle.y);
        points.push_back(cpd);
    }

    return points;
}

void LFOCurveEditor::commitUndoableCurveEdit(const std::vector<CurvePointData>& beforePoints,
                                             CurvePreset beforePreset,
                                             const juce::String& description) {
    if (!undoOwnerPath_.isValid() || undoModIndex_ < 0 || !modInfo_)
        return;

    auto afterPoints = snapshotCurvePoints();
    const auto afterPreset = modInfo_->curvePreset;
    if (beforePreset == afterPreset && sameCurvePoints(beforePoints, afterPoints))
        return;

    UndoManager::getInstance().executeCommand(std::make_unique<SetLFOCurveStateCommand>(
        undoOwnerPath_, undoModIndex_, beforePreset, beforePoints, afterPreset,
        std::move(afterPoints), description));
}

void LFOCurveEditor::syncFromModInfo() {
    if (!modInfo_)
        return;

    // Update local points from modInfo->curvePoints without rebuilding components
    // This is used for syncing with external editor during drag
    for (size_t i = 0; i < points_.size() && i < modInfo_->curvePoints.size(); ++i) {
        points_[i].x = static_cast<double>(modInfo_->curvePoints[i].phase);
        points_[i].y = static_cast<double>(modInfo_->curvePoints[i].value);
        points_[i].tension = static_cast<double>(modInfo_->curvePoints[i].tension);
        points_[i].curveType = intToCurveType(modInfo_->curvePoints[i].curveType);
        points_[i].inHandle.x = static_cast<double>(modInfo_->curvePoints[i].inHandleX);
        points_[i].inHandle.y = static_cast<double>(modInfo_->curvePoints[i].inHandleY);
        points_[i].outHandle.x = static_cast<double>(modInfo_->curvePoints[i].outHandleX);
        points_[i].outHandle.y = static_cast<double>(modInfo_->curvePoints[i].outHandleY);
    }

    // Update point component positions
    for (size_t i = 0; i < pointComponents_.size() && i < points_.size(); ++i) {
        pointComponents_[i]->updateFromPoint(points_[i]);
        int px = xToPixel(points_[i].x);
        int py = yToPixel(points_[i].y);
        pointComponents_[i]->setCentrePosition(px, py);
    }

    updateTensionHandlePositions();
    repaint();
}

void LFOCurveEditor::setModInfo(ModInfo* mod) {
    // Value-only refresh when the structure is unchanged. setModInfo is called
    // in a feedback loop after every edit (edit -> notifyWaveformChanged ->
    // panel refresh -> setModInfo with our own ModInfo). A full reload renumbers
    // point IDs and destroys/recreates the point components, which kills an
    // in-progress drag (the symptom: a point can be added but not moved). When
    // the point count is unchanged, update values in place and keep the live
    // components and IDs instead of rebuilding.
    if (mod && mod == modInfo_ && !points_.empty() && mod->curvePoints.size() == points_.size()) {
        for (size_t i = 0; i < points_.size(); ++i) {
            points_[i].x = static_cast<double>(mod->curvePoints[i].phase);
            points_[i].y = static_cast<double>(mod->curvePoints[i].value);
            points_[i].tension = static_cast<double>(mod->curvePoints[i].tension);
            points_[i].curveType = intToCurveType(mod->curvePoints[i].curveType);
            points_[i].inHandle.x = static_cast<double>(mod->curvePoints[i].inHandleX);
            points_[i].inHandle.y = static_cast<double>(mod->curvePoints[i].inHandleY);
            points_[i].outHandle.x = static_cast<double>(mod->curvePoints[i].outHandleX);
            points_[i].outHandle.y = static_cast<double>(mod->curvePoints[i].outHandleY);
        }
        points_.front().x = 0.0;
        points_.back().x = 1.0;
        updatePointPositions();
        updateTensionHandlePositions();
        repaint();
        return;
    }

    modInfo_ = mod;

    // Load curve points from ModInfo
    points_.clear();

    // Reset point ID counter on reload to keep IDs stable
    nextPointId_ = 1;

    if (mod && !mod->curvePoints.empty()) {
        // Load from ModInfo
        for (const auto& cp : mod->curvePoints) {
            CurvePoint point;
            point.id = nextPointId_++;
            point.x = static_cast<double>(cp.phase);
            point.y = static_cast<double>(cp.value);
            point.tension = static_cast<double>(cp.tension);
            point.curveType = intToCurveType(cp.curveType);
            point.inHandle.x = static_cast<double>(cp.inHandleX);
            point.inHandle.y = static_cast<double>(cp.inHandleY);
            point.outHandle.x = static_cast<double>(cp.outHandleX);
            point.outHandle.y = static_cast<double>(cp.outHandleY);
            points_.push_back(point);
        }
        // Sort by x position
        std::sort(points_.begin(), points_.end(),
                  [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        // Ensure first and last points are pinned to edges
        if (!points_.empty()) {
            points_.front().x = 0.0;
            points_.back().x = 1.0;
        }
    } else if (mod) {
        // Initialize with default triangle-like curve
        CurvePoint p1;
        p1.id = nextPointId_++;
        p1.x = 0.0;
        p1.y = 0.0;
        p1.curveType = CurveType::Linear;
        points_.push_back(p1);

        CurvePoint p2;
        p2.id = nextPointId_++;
        p2.x = 0.5;
        p2.y = 1.0;
        p2.curveType = CurveType::Linear;
        points_.push_back(p2);

        CurvePoint p3;
        p3.id = nextPointId_++;
        p3.x = 1.0;
        p3.y = 0.0;
        p3.curveType = CurveType::Linear;
        points_.push_back(p3);

        // Save defaults to ModInfo so mini waveform is synced immediately
        notifyWaveformChanged();
    }

    rebuildPointComponents();
    repaint();
}

double LFOCurveEditor::getPixelsPerX() const {
    // X is phase 0-1. Usable width is inset by kEdgePadding on each side so
    // extreme points are not flush against the content border.
    auto content = getContentBounds();
    double usable = content.getWidth() - 2.0 * kEdgePadding;
    return usable > 0.0 ? usable : 100.0;
}

double LFOCurveEditor::pixelToX(int px) const {
    auto content = getContentBounds();
    double usable = content.getWidth() - 2.0 * kEdgePadding;
    if (usable <= 0.0)
        return 0.0;
    return static_cast<double>(px - content.getX() - kEdgePadding) / usable;
}

int LFOCurveEditor::xToPixel(double x) const {
    auto content = getContentBounds();
    double usable = content.getWidth() - 2.0 * kEdgePadding;
    return content.getX() + kEdgePadding + static_cast<int>(x * usable);
}

double LFOCurveEditor::xToPixelF(double x) const {
    auto content = getContentBounds();
    double usable = content.getWidth() - 2.0 * kEdgePadding;
    return static_cast<double>(content.getX() + kEdgePadding) + x * usable;
}

double LFOCurveEditor::getPixelsPerY() const {
    // Y is value 0-1. Usable height is inset by kEdgePadding on each side so
    // extreme points (top/bottom) are not flush against the content border.
    auto content = getContentBounds();
    double usable = content.getHeight() - 2.0 * kEdgePadding;
    return usable > 0.0 ? usable : 100.0;
}

double LFOCurveEditor::pixelToY(int py) const {
    auto content = getContentBounds();
    double usable = content.getHeight() - 2.0 * kEdgePadding;
    if (usable <= 0.0)
        return 0.5;
    return 1.0 - static_cast<double>(py - content.getY() - kEdgePadding) / usable;
}

int LFOCurveEditor::yToPixel(double y) const {
    auto content = getContentBounds();
    double usable = content.getHeight() - 2.0 * kEdgePadding;
    return content.getY() + kEdgePadding + static_cast<int>((1.0 - y) * usable);
}

double LFOCurveEditor::yToPixelF(double y) const {
    auto content = getContentBounds();
    double usable = content.getHeight() - 2.0 * kEdgePadding;
    return static_cast<double>(content.getY() + kEdgePadding) + (1.0 - y) * usable;
}

const std::vector<CurvePoint>& LFOCurveEditor::getPoints() const {
    return points_;
}

void LFOCurveEditor::onPointAdded(double x, double y, CurveType curveType) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    // Clamp x to 0-1 range
    x = juce::jlimit(0.0, 1.0, x);
    y = juce::jlimit(0.0, 1.0, y);

    CurvePoint newPoint;
    newPoint.id = nextPointId_++;
    newPoint.x = x;
    newPoint.y = y;
    newPoint.curveType = curveType;

    // Insert in sorted order by x
    auto insertPos =
        std::lower_bound(points_.begin(), points_.end(), newPoint,
                         [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    points_.insert(insertPos, newPoint);

    rebuildPointComponents();
    repaint();  // Force full repaint after structural change
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::constrainPointPosition(uint32_t pointId, double& x, double& y) {
    // Clamp values
    x = juce::jlimit(0.0, 1.0, x);
    y = juce::jlimit(0.0, 1.0, y);

    // Find the point being moved and check if it's currently at an edge
    // We identify edge points by their current x value, not array position
    bool isEdgePoint = false;
    for (const auto& point : points_) {
        if (point.id == pointId) {
            // If this point is currently at x=0, pin it there
            if (std::abs(point.x) < 0.001) {
                x = 0.0;
                isEdgePoint = true;
            }
            // If this point is currently at x=1, pin it there
            else if (std::abs(point.x - 1.0) < 0.001) {
                x = 1.0;
                isEdgePoint = true;
            }
            break;
        }
    }

    // Apply snap to grid if enabled (only for non-edge points on X axis)
    if (snapX_ && !isEdgePoint && gridDivisionsX_ > 1) {
        double gridStep = 1.0 / gridDivisionsX_;
        x = std::round(x / gridStep) * gridStep;
        x = juce::jlimit(0.0, 1.0, x);
    }

    if (snapY_ && gridDivisionsY_ > 1) {
        double gridStep = 1.0 / gridDivisionsY_;
        y = std::round(y / gridStep) * gridStep;
        y = juce::jlimit(0.0, 1.0, y);
    }
}

void LFOCurveEditor::onPointMoved(uint32_t pointId, double newX, double newY) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    // Position is already constrained by constrainPointPosition

    for (auto& point : points_) {
        if (point.id == pointId) {
            point.x = newX;
            point.y = newY;
            break;
        }
    }

    // Re-sort points by x position
    std::sort(points_.begin(), points_.end(),
              [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    rebuildPointComponents();
    repaint();  // Force full repaint after structural change
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onPointDeleted(uint32_t pointId) {
    // Don't delete if only 2 points remain
    if (points_.size() <= 2)
        return;

    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    points_.erase(std::remove_if(points_.begin(), points_.end(),
                                 [pointId](const CurvePoint& p) { return p.id == pointId; }),
                  points_.end());

    if (selectedPointId_ == pointId) {
        selectedPointId_ = INVALID_CURVE_POINT_ID;
    }

    rebuildPointComponents();
    repaint();  // Force full repaint after structural change
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) {
    if (pointIds.empty())
        return;

    // Keep at least 2 points. If the selection would delete more, only remove
    // down to that floor (array order, so the rightmost survivors are kept).
    const size_t deletable = points_.size() > 2 ? points_.size() - 2 : 0;
    if (deletable == 0)
        return;

    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    size_t deleted = 0;
    points_.erase(std::remove_if(points_.begin(), points_.end(),
                                 [&](const CurvePoint& p) {
                                     if (deleted >= deletable || pointIds.count(p.id) == 0)
                                         return false;
                                     ++deleted;
                                     return true;
                                 }),
                  points_.end());

    if (deleted == 0)
        return;

    selectedPointId_ = INVALID_CURVE_POINT_ID;
    rebuildPointComponents();
    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onStepStamped(double gridStart, double gridEnd, double y, uint32_t prevPointId,
                                   double prevValue) {
    constexpr double kEps = 1e-6;
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    auto insertSorted = [this](double x, double yy, CurveType type) {
        CurvePoint p;
        p.id = nextPointId_++;
        p.x = juce::jlimit(0.0, 1.0, x);
        p.y = juce::jlimit(0.0, 1.0, yy);
        p.curveType = type;
        auto pos =
            std::lower_bound(points_.begin(), points_.end(), p,
                             [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        points_.insert(pos, p);
    };

    // Left cliff: flip the preceding point to Step so the segment into the cell
    // holds the baseline then jumps straight up to y.
    if (prevPointId != INVALID_CURVE_POINT_ID) {
        for (auto& p : points_)
            if (p.id == prevPointId)
                p.curveType = CurveType::Step;
    }

    // Cell's left edge at the click value (Step → flat top to gridEnd). If a
    // point already sits there, retarget it instead of stacking a duplicate.
    bool retargeted = false;
    for (auto& p : points_) {
        if (std::abs(p.x - gridStart) < kEps) {
            p.y = juce::jlimit(0.0, 1.0, y);
            p.curveType = CurveType::Step;
            retargeted = true;
            break;
        }
    }
    if (!retargeted)
        insertSorted(gridStart, y, CurveType::Step);

    // Right cliff: recover to the baseline at the cell's end (skip if a point
    // is already there). Linear so the segment leaving the cell flows normally.
    bool hasEnd = false;
    for (const auto& p : points_)
        if (std::abs(p.x - gridEnd) < kEps) {
            hasEnd = true;
            break;
        }
    if (gridEnd > gridStart + kEps && !hasEnd)
        insertSorted(gridEnd, prevValue, CurveType::Linear);

    rebuildPointComponents();
    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Stamp LFO Step");
}

void LFOCurveEditor::onPointSelected(uint32_t pointId) {
    selectedPointId_ = pointId;

    // Update selection state on point components
    for (auto& pc : pointComponents_) {
        pc->setSelected(pc->getPointId() == pointId);
    }

    repaint();
}

void LFOCurveEditor::onTensionChanged(uint32_t pointId, double tension) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    for (auto& point : points_) {
        if (point.id == pointId) {
            point.tension = tension;
            break;
        }
    }

    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onPointCurveTypeChanged(uint32_t pointId, CurveType newType) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    for (size_t i = 0; i < points_.size(); ++i) {
        if (points_[i].id != pointId)
            continue;
        DBG("[HardCorner] LFOCurveEditor::onPointCurveTypeChanged pointId="
            << static_cast<int>(pointId) << " oldType=" << getCurveTypeName(points_[i].curveType)
            << " newType=" << getCurveTypeName(newType));
        // Toggling to a hard corner keeps the segment's current bend: place the
        // corner apex where the smooth curve already sits (its on-curve midpoint)
        // instead of flattening the segment back to centre.
        if (newType == CurveType::HardCorner && i + 1 < points_.size()) {
            const auto [apexX, apexY] =
                getSegmentHandlePosition(points_[i], points_[i + 1], points_[i].tension);
            points_[i].curveType = newType;
            CurveHandleData outH;
            outH.x = apexX - points_[i].x;
            outH.y = apexY - points_[i].y;
            outH.linked = true;
            points_[i].outHandle = outH;
            CurveHandleData inH;
            inH.x = apexX - points_[i + 1].x;
            inH.y = apexY - points_[i + 1].y;
            inH.linked = true;
            points_[i + 1].inHandle = inH;
        } else {
            points_[i].curveType = newType;
        }
        break;
    }
    // notifyWaveformChanged persists to ModInfo; the base class refreshes
    // point/handle visuals after this returns.
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                                      const CurveHandleData& outHandle) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    for (auto& point : points_) {
        if (point.id == pointId) {
            point.inHandle = inHandle;
            point.outHandle = outHandle;
            break;
        }
    }

    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onSegmentShaperChanged(uint32_t leftPointId,
                                            const CurveHandleData& leftInHandle,
                                            const CurveHandleData& leftOutHandle,
                                            uint32_t rightPointId,
                                            const CurveHandleData& rightInHandle,
                                            const CurveHandleData& rightOutHandle, bool isPreview) {
    std::vector<CurvePointData> beforePoints;
    CurvePreset beforePreset = CurvePreset::Custom;
    if (isPreview) {
        if (!segmentShaperUndoActive_) {
            segmentShaperUndoActive_ = true;
            segmentShaperUndoBeforePoints_ = snapshotCurvePoints();
            segmentShaperUndoBeforePreset_ = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;
        }
    } else if (segmentShaperUndoActive_) {
        beforePoints = segmentShaperUndoBeforePoints_;
        beforePreset = segmentShaperUndoBeforePreset_;
        segmentShaperUndoActive_ = false;
        segmentShaperUndoBeforePoints_.clear();
    } else {
        beforePoints = snapshotCurvePoints();
        beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;
    }

    for (auto& point : points_) {
        if (point.id == leftPointId) {
            point.inHandle = leftInHandle;
            point.outHandle = leftOutHandle;
        } else if (point.id == rightPointId) {
            point.inHandle = rightInHandle;
            point.outHandle = rightOutHandle;
        }
    }

    repaint();
    notifyWaveformChanged();

    if (!isPreview)
        commitUndoableCurveEdit(beforePoints, beforePreset, "Edit LFO Curve");
}

void LFOCurveEditor::onPointDragPreview(uint32_t pointId, double newX, double newY) {
    // Update ModInfo during drag for fluid mini waveform preview
    if (!modInfo_)
        return;

    // Position is already constrained by constrainPointPosition in the base class
    // Find and update the point in ModInfo by index
    bool found = false;
    for (size_t i = 0; i < points_.size() && i < modInfo_->curvePoints.size(); ++i) {
        if (points_[i].id == pointId) {
            modInfo_->curvePoints[i].phase = static_cast<float>(newX);
            modInfo_->curvePoints[i].value = static_cast<float>(newY);
            modInfo_->curvePoints[i].inHandleX = static_cast<float>(points_[i].inHandle.x);
            modInfo_->curvePoints[i].inHandleY = static_cast<float>(points_[i].inHandle.y);
            modInfo_->curvePoints[i].outHandleX = static_cast<float>(points_[i].outHandle.x);
            modInfo_->curvePoints[i].outHandleY = static_cast<float>(points_[i].outHandle.y);
            found = true;
            break;
        }
    }
    (void)found;

    if (onDragPreview) {
        onDragPreview();
    }
}

void LFOCurveEditor::onTensionDragPreview(uint32_t pointId, double tension) {
    // Update ModInfo during drag for fluid mini waveform preview
    if (!modInfo_)
        return;

    // Find and update the tension in ModInfo
    for (size_t i = 0; i < points_.size(); ++i) {
        if (points_[i].id == pointId && i < modInfo_->curvePoints.size()) {
            modInfo_->curvePoints[i].tension = static_cast<float>(tension);
            break;
        }
    }

    if (onDragPreview) {
        onDragPreview();
    }
}

bool LFOCurveEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        if (UndoManager::getInstance().undo())
            setModInfo(modInfo_);
        return true;
    }

    if (key ==
        juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier,
                       0)) {
        if (UndoManager::getInstance().redo())
            setModInfo(modInfo_);
        return true;
    }

    if (key == juce::KeyPress('c') || key == juce::KeyPress('C')) {
        showCrosshair_ = !showCrosshair_;
        repaint();
        return true;
    }
    return CurveEditorBase::keyPressed(key);
}

void LFOCurveEditor::timerCallback() {
    if (!modInfo_)
        return;

    bool needsRepaint = false;

    // Track trigger events
    if (modInfo_->triggerCount != lastSeenTriggerCount_) {
        lastSeenTriggerCount_ = modInfo_->triggerCount;
        triggerHoldFrames_ = 4;  // Show for ~130ms at 30fps
        needsRepaint = true;
    }
    if (triggerHoldFrames_ > 0) {
        triggerHoldFrames_--;
        needsRepaint = true;
    }

    // Only repaint if phase/value changed (and only the indicator region)
    float newPhase = modInfo_->phase;
    float newValue = modInfo_->value;

    if (std::abs(newPhase - lastPhase_) > 0.001f || std::abs(newValue - lastValue_) > 0.001f) {
        lastPhase_ = newPhase;
        lastValue_ = newValue;
        // Full repaint: a tight partial region clips the curve and handles the dot
        // passes over, leaving ghost arcs. The editor is small, so 30fps full
        // repaints are cheap.
        repaint();
        needsRepaint = false;
    }

    if (needsRepaint)
        repaint();
}

juce::Rectangle<int> LFOCurveEditor::getIndicatorBounds() const {
    // Use the padded x/y mapping so the indicator tracks the curve position correctly.
    int x = static_cast<int>(xToPixelF(static_cast<double>(lastPhase_)));
    int y = static_cast<int>(yToPixelF(static_cast<double>(lastValue_)));

    // Return a small region around the indicator dot
    constexpr int margin = 8;
    return juce::Rectangle<int>(x - margin, y - margin, margin * 2, margin * 2);
}

void LFOCurveEditor::paint(juce::Graphics& g) {
    // Let base class paint background, grid, curve
    CurveEditorBase::paint(g);

    // Paint phase indicator on top
    paintPhaseIndicator(g);
}

void LFOCurveEditor::paintPhaseIndicator(juce::Graphics& g) {
    if (!modInfo_)
        return;

    auto content = getContentBounds();
    float phase = modInfo_->phase;
    float value = modInfo_->value;

    // Use padded x/y mapping so the indicator follows the curve correctly.
    int x = static_cast<int>(xToPixelF(static_cast<double>(phase)));
    int y = static_cast<int>(yToPixelF(static_cast<double>(value)));

    // Draw crosshair lines (toggle with 'C' key)
    if (showCrosshair_) {
        g.setColour(curveColour_.withAlpha(0.4f));
        g.drawVerticalLine(x, static_cast<float>(content.getY()),
                           static_cast<float>(content.getBottom()));
        g.drawHorizontalLine(y, static_cast<float>(content.getX()),
                             static_cast<float>(content.getRight()));
    }

    // Draw indicator dot
    constexpr float dotSize = 5.0f;
    constexpr float dotRadius = dotSize / 2.0f;
    g.setColour(curveColour_);
    g.fillEllipse(static_cast<float>(x) - dotRadius, static_cast<float>(y) - dotRadius, dotSize,
                  dotSize);

    // Draw white outline
    g.setColour(juce::Colours::white);
    g.drawEllipse(static_cast<float>(x) - dotRadius, static_cast<float>(y) - dotRadius, dotSize,
                  dotSize, 1.0f);

    // Draw trigger indicator dot in top-right corner
    constexpr float trigDotRadius = 3.0f;
    auto trigBounds = juce::Rectangle<float>(
        static_cast<float>(content.getRight()) - trigDotRadius * 2 - 4.0f,
        static_cast<float>(content.getY()) + 4.0f, trigDotRadius * 2, trigDotRadius * 2);

    if (triggerHoldFrames_ > 0) {
        g.setColour(curveColour_);
        g.fillEllipse(trigBounds);
    } else {
        g.setColour(curveColour_.withAlpha(0.3f));
        g.drawEllipse(trigBounds, 1.0f);
    }
}

void LFOCurveEditor::paintGrid(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    float width = static_cast<float>(bounds.getWidth());
    float height = static_cast<float>(bounds.getHeight());

    // Horizontal grid lines (value divisions) - placed using the padded y mapping
    // so they align with the curve and point positions.
    for (int i = 1; i < gridDivisionsY_; ++i) {
        // Map grid fraction to data value: i/gridDivisionsY_ from top is value (1 - i/grid)
        double value = 1.0 - static_cast<double>(i) / gridDivisionsY_;
        int y = static_cast<int>(yToPixelF(value));
        // Center line is brighter
        bool isCenter = (i * 2 == gridDivisionsY_);
        g.setColour(juce::Colour(isCenter ? 0x20FFFFFF : 0x10FFFFFF));
        g.drawHorizontalLine(y, 0.0f, width);
    }

    // Vertical grid lines (phase divisions) — placed using the padded x mapping
    // so they align with the curve and point positions.
    for (int i = 1; i < gridDivisionsX_; ++i) {
        double phase = static_cast<double>(i) / gridDivisionsX_;
        int x = static_cast<int>(xToPixelF(phase));
        bool isCenter = (i * 2 == gridDivisionsX_);
        g.setColour(juce::Colour(isCenter ? 0x20FFFFFF : 0x10FFFFFF));
        g.drawVerticalLine(x, 0.0f, height);
    }

    // Draw loop region if enabled and modInfo has loop region
    if (showLoopRegion_ && modInfo_ && modInfo_->useLoopRegion) {
        paintLoopRegion(g);
    }
}

void LFOCurveEditor::paintLoopRegion(juce::Graphics& g) {
    if (!modInfo_)
        return;

    auto content = getContentBounds();
    float loopStartX = static_cast<float>(xToPixelF(static_cast<double>(modInfo_->loopStart)));
    float loopEndX = static_cast<float>(xToPixelF(static_cast<double>(modInfo_->loopEnd)));

    // Shade areas outside the loop region
    g.setColour(juce::Colour(0x30000000));
    if (loopStartX > content.getX()) {
        g.fillRect(juce::Rectangle<float>(
            static_cast<float>(content.getX()), static_cast<float>(content.getY()),
            loopStartX - content.getX(), static_cast<float>(content.getHeight())));
    }
    if (loopEndX < content.getRight()) {
        g.fillRect(juce::Rectangle<float>(loopEndX, static_cast<float>(content.getY()),
                                          content.getRight() - loopEndX,
                                          static_cast<float>(content.getHeight())));
    }

    // Draw loop region markers
    g.setColour(curveColour_.withAlpha(0.7f));
    g.drawVerticalLine(static_cast<int>(loopStartX), static_cast<float>(content.getY()),
                       static_cast<float>(content.getBottom()));
    g.drawVerticalLine(static_cast<int>(loopEndX), static_cast<float>(content.getY()),
                       static_cast<float>(content.getBottom()));

    // Draw small triangular markers at top
    constexpr float markerSize = 6.0f;
    juce::Path startMarker;
    startMarker.addTriangle(loopStartX, static_cast<float>(content.getY()), loopStartX + markerSize,
                            static_cast<float>(content.getY()), loopStartX,
                            static_cast<float>(content.getY()) + markerSize);
    g.fillPath(startMarker);

    juce::Path endMarker;
    endMarker.addTriangle(loopEndX, static_cast<float>(content.getY()), loopEndX - markerSize,
                          static_cast<float>(content.getY()), loopEndX,
                          static_cast<float>(content.getY()) + markerSize);
    g.fillPath(endMarker);
}

int LFOCurveEditor::loopMarkerAtPixel(int px, int py) const {
    if (!showLoopRegion_ || !modInfo_ || !modInfo_->useLoopRegion)
        return 0;

    auto content = getContentBounds();
    // Only grab via the handles in the top strip so points elsewhere on the
    // curve stay editable even near the loop boundaries.
    constexpr int kTopStrip = 12;
    if (py > content.getY() + kTopStrip)
        return 0;

    const int startX = static_cast<int>(std::round(xToPixelF(modInfo_->loopStart)));
    const int endX = static_cast<int>(std::round(xToPixelF(modInfo_->loopEnd)));
    constexpr int kHitTol = 6;
    const int dStart = std::abs(px - startX);
    const int dEnd = std::abs(px - endX);
    if (dStart <= kHitTol && dStart <= dEnd)
        return 1;
    if (dEnd <= kHitTol)
        return 2;
    return 0;
}

void LFOCurveEditor::mouseDown(const juce::MouseEvent& e) {
    if (int marker = loopMarkerAtPixel(e.x, e.y)) {
        draggingLoopMarker_ = marker;
        return;  // consume — don't let the base add/select a curve point
    }
    CurveEditorBase::mouseDown(e);
}

void LFOCurveEditor::mouseDrag(const juce::MouseEvent& e) {
    if (draggingLoopMarker_ != 0 && modInfo_) {
        constexpr float kMinGap = 0.02f;
        float phase = static_cast<float>(juce::jlimit(0.0, 1.0, pixelToX(e.x)));
        if (snapLoop_ && gridDivisionsX_ > 1) {
            const double step = 1.0 / gridDivisionsX_;
            phase = static_cast<float>(juce::jlimit(0.0, 1.0, std::round(phase / step) * step));
        }
        if (draggingLoopMarker_ == 1)
            modInfo_->loopStart = juce::jlimit(0.0f, modInfo_->loopEnd - kMinGap, phase);
        else
            modInfo_->loopEnd = juce::jlimit(modInfo_->loopStart + kMinGap, 1.0f, phase);
        repaint();
        if (onDragPreview)
            onDragPreview();
        return;
    }
    CurveEditorBase::mouseDrag(e);
}

void LFOCurveEditor::mouseUp(const juce::MouseEvent& e) {
    if (draggingLoopMarker_ != 0) {
        draggingLoopMarker_ = 0;
        // loopStart/loopEnd already live on modInfo_; persist + resync.
        notifyWaveformChanged();
        return;
    }
    CurveEditorBase::mouseUp(e);
}

void LFOCurveEditor::notifyWaveformChanged() {
    // Save curve points to ModInfo
    if (modInfo_) {
        modInfo_->curvePoints.clear();
        for (const auto& p : points_) {
            CurvePointData cpd;
            cpd.phase = static_cast<float>(p.x);
            cpd.value = static_cast<float>(p.y);
            cpd.tension = static_cast<float>(p.tension);
            cpd.curveType = curveTypeToInt(p.curveType);
            cpd.inHandleX = static_cast<float>(p.inHandle.x);
            cpd.inHandleY = static_cast<float>(p.inHandle.y);
            cpd.outHandleX = static_cast<float>(p.outHandle.x);
            cpd.outHandleY = static_cast<float>(p.outHandle.y);
            modInfo_->curvePoints.push_back(cpd);
        }
        DBG("[HardCorner] LFOCurveEditor::notifyWaveformChanged modId="
            << static_cast<int>(modInfo_->id) << " name=" << modInfo_->name
            << " points=" << static_cast<int>(modInfo_->curvePoints.size()) << " editorTypes=["
            << formatCurvePointTypes(points_) << "] modTypes=["
            << formatModCurvePointTypes(modInfo_->curvePoints) << "]");
    }

    if (onWaveformChanged) {
        onWaveformChanged();
    }
}

void LFOCurveEditor::loadPreset(CurvePreset preset) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    points_.clear();
    nextPointId_ = 1;

    auto addPoint = [this](double x, double y, double tension = 0.0) {
        CurvePoint p;
        p.id = nextPointId_++;
        p.x = x;
        p.y = y;
        p.tension = tension;
        p.curveType = CurveType::Linear;
        points_.push_back(p);
    };

    switch (preset) {
        case CurvePreset::Triangle:
            addPoint(0.0, 0.0);
            addPoint(0.5, 1.0);
            addPoint(1.0, 0.0);
            break;

        case CurvePreset::Sine: {
            // Sine wave with 5 points + tension for smooth curves
            // Tension shapes the curve between points:
            //   negative = ease-out (fast start, slow end)
            //   positive = ease-in (slow start, fast end)
            addPoint(0.0, 0.5, -0.7);  // Start at mid, rising with ease-out
            addPoint(0.25, 1.0, 0.7);  // Peak, falling with ease-in
            addPoint(0.5, 0.5, -0.7);  // Mid crossing, falling with ease-out
            addPoint(0.75, 0.0, 0.7);  // Trough, rising with ease-in
            addPoint(1.0, 0.5);        // End at mid
            break;
        }

        case CurvePreset::RampUp:
            addPoint(0.0, 0.0);
            addPoint(1.0, 1.0);
            break;

        case CurvePreset::RampDown:
            addPoint(0.0, 1.0);
            addPoint(1.0, 0.0);
            break;

        case CurvePreset::SCurve:
            // S-curve with tension for smooth shape
            addPoint(0.0, 0.0, 0.8);   // Ease-in at start
            addPoint(0.5, 0.5, -0.8);  // Ease-out toward end
            addPoint(1.0, 1.0);
            break;

        case CurvePreset::Exponential:
            // Exponential curve using tension
            addPoint(0.0, 0.0, 1.2);  // Strong ease-in
            addPoint(1.0, 1.0);
            break;

        case CurvePreset::Logarithmic:
            // Logarithmic curve using tension
            addPoint(0.0, 0.0, -1.2);  // Strong ease-out
            addPoint(1.0, 1.0);
            break;

        case CurvePreset::Custom:
        default:
            // Default triangle
            addPoint(0.0, 0.0);
            addPoint(0.5, 1.0);
            addPoint(1.0, 0.0);
            break;
    }

    if (modInfo_) {
        modInfo_->curvePreset = preset;
    }

    rebuildPointComponents();
    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Load LFO Curve Preset");
}

void LFOCurveEditor::loadCurvePoints(const std::vector<CurvePointData>& points) {
    const auto beforePoints = snapshotCurvePoints();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    points_.clear();
    nextPointId_ = 1;

    for (const auto& cp : points) {
        CurvePoint point;
        point.id = nextPointId_++;
        point.x = juce::jlimit(0.0, 1.0, static_cast<double>(cp.phase));
        point.y = juce::jlimit(0.0, 1.0, static_cast<double>(cp.value));
        point.tension = static_cast<double>(cp.tension);
        point.curveType = intToCurveType(cp.curveType);
        point.inHandle.x = static_cast<double>(cp.inHandleX);
        point.inHandle.y = static_cast<double>(cp.inHandleY);
        point.outHandle.x = static_cast<double>(cp.outHandleX);
        point.outHandle.y = static_cast<double>(cp.outHandleY);
        points_.push_back(point);
    }

    std::sort(points_.begin(), points_.end(),
              [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    if (points_.size() < 2) {
        loadPreset(CurvePreset::Triangle);
        return;
    }

    points_.front().x = 0.0;
    points_.back().x = 1.0;

    if (modInfo_) {
        modInfo_->curvePreset = CurvePreset::Custom;
    }

    rebuildPointComponents();
    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Load LFO Curve Preset");
}

}  // namespace magda
