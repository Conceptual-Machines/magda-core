#include "LFOCurveEditor.hpp"

#include <algorithm>
#include <cmath>

namespace magda {

LFOCurveEditor::LFOCurveEditor() {
    setName("LFOCurveEditor");

    // Start timer for phase indicator animation
    startTimer(33);  // 30 FPS

    rebuildPointComponents();
}

LFOCurveEditor::~LFOCurveEditor() {
    stopTimer();
}

void LFOCurveEditor::setModInfo(ModInfo* mod) {
    modInfo_ = mod;

    // Load curve points from ModInfo
    points_.clear();

    if (mod && !mod->curvePoints.empty()) {
        // Load from ModInfo
        for (const auto& cp : mod->curvePoints) {
            CurvePoint point;
            point.id = nextPointId_++;
            point.x = static_cast<double>(cp.phase);
            point.y = static_cast<double>(cp.value);
            point.tension = static_cast<double>(cp.tension);
            point.curveType = CurveType::Linear;
            points_.push_back(point);
        }
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

void LFOCurveEditor::timerCallback() {
    // Repaint to update phase indicator
    repaint();
}

void LFOCurveEditor::paint(juce::Graphics& g) {
    // Call base class paint for curve rendering
    CurveEditorBase::paint(g);

    // Draw phase indicator if we have modInfo with current phase
    if (modInfo_ && getWidth() > 0 && getHeight() > 0) {
        float phase = modInfo_->phase;
        float value = modInfo_->value;

        // Calculate position
        int x = xToPixel(static_cast<double>(phase));
        int y = yToPixel(static_cast<double>(value));

        // Draw indicator dot
        g.setColour(curveColour_);
        g.fillEllipse(static_cast<float>(x - 4), static_cast<float>(y - 4), 8.0f, 8.0f);

        // Draw white outline
        g.setColour(juce::Colours::white);
        g.drawEllipse(static_cast<float>(x - 4), static_cast<float>(y - 4), 8.0f, 8.0f, 1.5f);
    }
}

double LFOCurveEditor::getPixelsPerX() const {
    // X is phase 0-1, so pixels per X = content width
    auto content = getContentBounds();
    return content.getWidth() > 0 ? static_cast<double>(content.getWidth()) : 100.0;
}

double LFOCurveEditor::pixelToX(int px) const {
    auto content = getContentBounds();
    if (content.getWidth() <= 0)
        return 0.0;
    return static_cast<double>(px - content.getX()) / content.getWidth();
}

int LFOCurveEditor::xToPixel(double x) const {
    auto content = getContentBounds();
    return content.getX() + static_cast<int>(x * content.getWidth());
}

const std::vector<CurvePoint>& LFOCurveEditor::getPoints() const {
    return points_;
}

void LFOCurveEditor::onPointAdded(double x, double y, CurveType curveType) {
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
    notifyWaveformChanged();
}

void LFOCurveEditor::onPointMoved(uint32_t pointId, double newX, double newY) {
    // Clamp values
    newX = juce::jlimit(0.0, 1.0, newX);
    newY = juce::jlimit(0.0, 1.0, newY);

    // Check if this is the first or last point (they should be pinned to edges)
    bool isFirstPoint = !points_.empty() && points_.front().id == pointId;
    bool isLastPoint = !points_.empty() && points_.back().id == pointId;

    for (auto& point : points_) {
        if (point.id == pointId) {
            // First point is pinned to x=0, last point to x=1
            if (isFirstPoint) {
                point.x = 0.0;
            } else if (isLastPoint) {
                point.x = 1.0;
            } else {
                point.x = newX;
            }
            point.y = newY;
            break;
        }
    }

    // Re-sort points by x position
    std::sort(points_.begin(), points_.end(),
              [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    rebuildPointComponents();
    notifyWaveformChanged();
}

void LFOCurveEditor::onPointDeleted(uint32_t pointId) {
    // Don't delete if only 2 points remain
    if (points_.size() <= 2)
        return;

    points_.erase(std::remove_if(points_.begin(), points_.end(),
                                 [pointId](const CurvePoint& p) { return p.id == pointId; }),
                  points_.end());

    if (selectedPointId_ == pointId) {
        selectedPointId_ = INVALID_CURVE_POINT_ID;
    }

    rebuildPointComponents();
    notifyWaveformChanged();
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
    for (auto& point : points_) {
        if (point.id == pointId) {
            point.tension = tension;
            break;
        }
    }

    repaint();
    notifyWaveformChanged();
}

void LFOCurveEditor::onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                                      const CurveHandleData& outHandle) {
    for (auto& point : points_) {
        if (point.id == pointId) {
            point.inHandle = inHandle;
            point.outHandle = outHandle;
            break;
        }
    }

    repaint();
    notifyWaveformChanged();
}

void LFOCurveEditor::onPointDragPreview(uint32_t pointId, double newX, double newY) {
    // Update ModInfo during drag for fluid mini waveform preview
    if (!modInfo_)
        return;

    // Check if this is the first or last point (they should be pinned to edges)
    bool isFirstPoint = !points_.empty() && points_.front().id == pointId;
    bool isLastPoint = !points_.empty() && points_.back().id == pointId;

    // Pin first/last points to edges
    if (isFirstPoint) {
        newX = 0.0;
    } else if (isLastPoint) {
        newX = 1.0;
    }

    // Find and update the point in ModInfo by index
    for (size_t i = 0; i < points_.size() && i < modInfo_->curvePoints.size(); ++i) {
        if (points_[i].id == pointId) {
            modInfo_->curvePoints[i].phase = static_cast<float>(newX);
            modInfo_->curvePoints[i].value = static_cast<float>(newY);
            return;
        }
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
            return;
        }
    }
}

void LFOCurveEditor::paintGrid(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Horizontal center line (0.5 value)
    g.setColour(juce::Colour(0x20FFFFFF));
    int centerY = bounds.getHeight() / 2;
    g.drawHorizontalLine(centerY, 0.0f, static_cast<float>(bounds.getWidth()));

    // Quarter lines (0.25, 0.75 value)
    g.setColour(juce::Colour(0x10FFFFFF));
    g.drawHorizontalLine(bounds.getHeight() / 4, 0.0f, static_cast<float>(bounds.getWidth()));
    g.drawHorizontalLine(bounds.getHeight() * 3 / 4, 0.0f, static_cast<float>(bounds.getWidth()));

    // Vertical quarter lines (phase 0.25, 0.5, 0.75)
    g.setColour(juce::Colour(0x10FFFFFF));
    for (int i = 1; i < 4; ++i) {
        int x = bounds.getWidth() * i / 4;
        g.drawVerticalLine(x, 0.0f, static_cast<float>(bounds.getHeight()));
    }

    // Phase 0.5 line (center) slightly brighter
    g.setColour(juce::Colour(0x20FFFFFF));
    g.drawVerticalLine(bounds.getWidth() / 2, 0.0f, static_cast<float>(bounds.getHeight()));
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
            modInfo_->curvePoints.push_back(cpd);
        }
    }

    if (onWaveformChanged) {
        onWaveformChanged();
    }
}

}  // namespace magda
