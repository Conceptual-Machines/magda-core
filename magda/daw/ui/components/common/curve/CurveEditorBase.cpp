#include "CurveEditorBase.hpp"

#include <algorithm>

namespace magda {

CurveEditorBase::CurveEditorBase() {
    setName("CurveEditorBase");
}

CurveEditorBase::~CurveEditorBase() = default;

void CurveEditorBase::paint(juce::Graphics& g) {
    // Background
    g.fillAll(juce::Colour(0xFF1A1A1A));

    // Grid
    paintGrid(g);

    // Curve
    paintCurve(g);

    // Drawing preview
    if (isDrawing_) {
        paintDrawingPreview(g);
    }
}

void CurveEditorBase::resized() {
    updatePointPositions();
}

void CurveEditorBase::paintGrid(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Subtle horizontal grid lines (value levels at 25%, 50%, 75%)
    g.setColour(juce::Colour(0x15FFFFFF));
    for (int i = 1; i < 4; ++i) {
        int y = bounds.getHeight() * i / 4;
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));
    }
}

void CurveEditorBase::paintCurve(juce::Graphics& g) {
    const auto& points = getPoints();
    if (points.empty())
        return;

    // Create path for curve
    juce::Path curvePath;
    bool pathStarted = false;

    // Handle edge behavior based on loop mode
    if (shouldLoop()) {
        // For looping (LFO): Connect last point back to first
        if (!points.empty()) {
            auto [firstX, firstY] = getEffectivePosition(points.front());
            auto [lastX, lastY] = getEffectivePosition(points.back());

            // Start at left edge with wrapped value from last point
            int startPixelY = yToPixel(lastY);
            curvePath.startNewSubPath(0.0f, static_cast<float>(startPixelY));

            // Draw to first point
            int firstPixelX = xToPixel(firstX);
            int firstPixelY = yToPixel(firstY);
            curvePath.lineTo(static_cast<float>(firstPixelX), static_cast<float>(firstPixelY));
            pathStarted = true;
        }
    } else {
        // For non-looping (automation): Extend from left edge at first point's value
        if (!points.empty()) {
            auto [firstX, firstY] = getEffectivePosition(points.front());
            int firstPixelX = xToPixel(firstX);
            int firstPixelY = yToPixel(firstY);

            if (firstPixelX > 0) {
                curvePath.startNewSubPath(0.0f, static_cast<float>(firstPixelY));
                curvePath.lineTo(static_cast<float>(firstPixelX), static_cast<float>(firstPixelY));
                pathStarted = true;
            }
        }
    }

    // Draw between points
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        auto [x, y] = getEffectivePosition(p);
        int pixelX = xToPixel(x);
        int pixelY = yToPixel(y);

        if (!pathStarted) {
            curvePath.startNewSubPath(static_cast<float>(pixelX), static_cast<float>(pixelY));
            pathStarted = true;
        } else if (i > 0) {
            const auto& prevP = points[i - 1];

            // Get effective tension (use preview if dragging this segment)
            double effectiveTension = prevP.tension;
            if (tensionPreviewPointId_ != INVALID_CURVE_POINT_ID &&
                prevP.id == tensionPreviewPointId_) {
                effectiveTension = tensionPreviewValue_;
            }

            renderCurveSegment(curvePath, prevP, p, effectiveTension);
        }
    }

    // Handle edge behavior at the end
    if (shouldLoop()) {
        // For looping: Connect last point to wrapped first point at right edge
        if (!points.empty()) {
            auto [firstX, firstY] = getEffectivePosition(points.front());
            int width = getWidth();
            int endPixelY = yToPixel(firstY);
            curvePath.lineTo(static_cast<float>(width), static_cast<float>(endPixelY));
        }
    } else {
        // For non-looping: Extend to right edge at last point's value
        if (!points.empty()) {
            auto [lastX, lastY] = getEffectivePosition(points.back());
            juce::ignoreUnused(lastX);
            int lastPixelY = yToPixel(lastY);
            int width = getWidth();
            curvePath.lineTo(static_cast<float>(width), static_cast<float>(lastPixelY));
        }
    }

    // Draw the curve
    g.setColour(curveColour_);
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));

    // Optional: fill under curve
    juce::Path fillPath = curvePath;
    fillPath.lineTo(static_cast<float>(getWidth()), static_cast<float>(getHeight()));
    fillPath.lineTo(0.0f, static_cast<float>(getHeight()));
    fillPath.closeSubPath();
    g.setColour(curveColour_.withAlpha(0.13f));
    g.fillPath(fillPath);
}

void CurveEditorBase::renderCurveSegment(juce::Path& path, const CurvePoint& p1,
                                         const CurvePoint& p2, double effectiveTension) {
    auto [x1, y1] = getEffectivePosition(p1);
    auto [x2, y2] = getEffectivePosition(p2);
    int pixelX2 = xToPixel(x2);
    int pixelY2 = yToPixel(y2);

    switch (p1.curveType) {
        case CurveType::Linear: {
            if (std::abs(effectiveTension) < 0.001) {
                // Pure linear
                path.lineTo(static_cast<float>(pixelX2), static_cast<float>(pixelY2));
            } else {
                // Tension-based curve - draw as series of line segments
                const int NUM_SEGMENTS = 16;
                for (int seg = 1; seg <= NUM_SEGMENTS; ++seg) {
                    double t = static_cast<double>(seg) / NUM_SEGMENTS;

                    // Apply tension curve (tension can be -3 to +3 with Shift)
                    double curvedT;
                    if (effectiveTension > 0) {
                        curvedT = std::pow(t, 1.0 + effectiveTension * 2.0);
                    } else {
                        curvedT = 1.0 - std::pow(1.0 - t, 1.0 - effectiveTension * 2.0);
                    }

                    double segY = y1 + curvedT * (y2 - y1);
                    double segX = x1 + t * (x2 - x1);

                    float segPixelX = static_cast<float>(xToPixel(segX));
                    float segPixelY = static_cast<float>(yToPixel(segY));

                    path.lineTo(segPixelX, segPixelY);
                }
            }
            break;
        }

        case CurveType::Bezier: {
            // Calculate control points using effective positions
            int pixelX1 = xToPixel(x1);
            int pixelY1 = yToPixel(y1);

            float cp1X = pixelX1 + static_cast<float>(p1.outHandle.x * getPixelsPerX());
            float cp1Y = pixelY1 - static_cast<float>(p1.outHandle.y * getPixelsPerY());
            float cp2X = pixelX2 + static_cast<float>(p2.inHandle.x * getPixelsPerX());
            float cp2Y = pixelY2 - static_cast<float>(p2.inHandle.y * getPixelsPerY());

            path.cubicTo(cp1X, cp1Y, cp2X, cp2Y, static_cast<float>(pixelX2),
                         static_cast<float>(pixelY2));
            break;
        }

        case CurveType::Step:
            // Step: horizontal then vertical
            path.lineTo(static_cast<float>(pixelX2), path.getCurrentPosition().y);
            path.lineTo(static_cast<float>(pixelX2), static_cast<float>(pixelY2));
            break;
    }
}

void CurveEditorBase::paintDrawingPreview(juce::Graphics& g) {
    if (drawMode_ == CurveDrawMode::Pencil && !drawingPath_.empty()) {
        g.setColour(juce::Colour(0xAAFFFFFF));
        for (size_t i = 1; i < drawingPath_.size(); ++i) {
            g.drawLine(static_cast<float>(drawingPath_[i - 1].x),
                       static_cast<float>(drawingPath_[i - 1].y),
                       static_cast<float>(drawingPath_[i].x), static_cast<float>(drawingPath_[i].y),
                       2.0f);
        }
    } else if (drawMode_ == CurveDrawMode::Line && isDrawing_) {
        g.setColour(juce::Colour(0xAAFFFFFF));
        auto mousePos = getMouseXYRelative();
        g.drawLine(static_cast<float>(lineStartPoint_.x), static_cast<float>(lineStartPoint_.y),
                   static_cast<float>(mousePos.x), static_cast<float>(mousePos.y), 2.0f);
    }
}

void CurveEditorBase::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        switch (drawMode_) {
            case CurveDrawMode::Select:
                // Click on empty area - subclass handles deselection
                break;

            case CurveDrawMode::Pencil:
                isDrawing_ = true;
                drawingPath_.clear();
                drawingPath_.push_back(e.getPosition());
                break;

            case CurveDrawMode::Line:
                isDrawing_ = true;
                lineStartPoint_ = e.getPosition();
                break;

            case CurveDrawMode::Curve:
                // Similar to pencil but creates bezier points
                isDrawing_ = true;
                drawingPath_.clear();
                drawingPath_.push_back(e.getPosition());
                break;
        }
    }
}

void CurveEditorBase::mouseDrag(const juce::MouseEvent& e) {
    if (!isDrawing_)
        return;

    if (drawMode_ == CurveDrawMode::Pencil || drawMode_ == CurveDrawMode::Curve) {
        drawingPath_.push_back(e.getPosition());
        repaint();
    } else if (drawMode_ == CurveDrawMode::Line) {
        repaint();  // Redraw line preview
    }
}

void CurveEditorBase::mouseUp(const juce::MouseEvent& e) {
    if (isDrawing_) {
        isDrawing_ = false;

        switch (drawMode_) {
            case CurveDrawMode::Pencil:
                createPointsFromDrawingPath();
                break;

            case CurveDrawMode::Line: {
                // Create two points: start and end
                double startX = pixelToX(lineStartPoint_.x);
                double startY = pixelToY(lineStartPoint_.y);
                double endX = pixelToX(e.x);
                double endY = pixelToY(e.y);

                onPointAdded(startX, startY, CurveType::Linear);
                onPointAdded(endX, endY, CurveType::Linear);
                break;
            }

            case CurveDrawMode::Curve:
                createPointsFromDrawingPath();
                break;

            default:
                break;
        }

        drawingPath_.clear();
        repaint();
    }
}

void CurveEditorBase::mouseDoubleClick(const juce::MouseEvent& e) {
    // Double-click to add a point
    double x = pixelToX(e.x);
    double y = pixelToY(e.y);

    // Snap if enabled
    if (snapXToGrid) {
        x = snapXToGrid(x);
    }

    CurveType curveType =
        (drawMode_ == CurveDrawMode::Curve) ? CurveType::Bezier : CurveType::Linear;

    onPointAdded(x, y, curveType);
}

bool CurveEditorBase::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        // Subclass should handle deletion of selected points
        return true;
    }
    return false;
}

double CurveEditorBase::pixelToY(int py) const {
    int height = getHeight();
    if (height <= 0)
        return 0.5;
    return 1.0 - (static_cast<double>(py) / height);
}

int CurveEditorBase::yToPixel(double y) const {
    return static_cast<int>((1.0 - y) * getHeight());
}

std::pair<double, double> CurveEditorBase::getEffectivePosition(const CurvePoint& p) const {
    if (previewPointId_ != INVALID_CURVE_POINT_ID && p.id == previewPointId_) {
        return {previewX_, previewY_};
    }
    return {p.x, p.y};
}

void CurveEditorBase::rebuildPointComponents() {
    pointComponents_.clear();
    handleComponents_.clear();
    tensionHandles_.clear();

    const auto& points = getPoints();
    for (const auto& point : points) {
        auto pc = std::make_unique<CurvePointComponent>(point.id, this);
        pc->updateFromPoint(point);

        // Set callbacks
        pc->onPointSelected = [this](uint32_t pointId) { onPointSelected(pointId); };

        pc->onPointMoved = [this](uint32_t pointId, double newX, double newY) {
            onPointMoved(pointId, newX, newY);
        };

        pc->onPointDragPreview = [this](uint32_t pointId, double newX, double newY) {
            // Update preview state directly
            previewPointId_ = pointId;
            previewX_ = newX;
            previewY_ = newY;

            // Update the point component position
            for (auto& ptComp : pointComponents_) {
                if (ptComp->getPointId() == pointId) {
                    int px = xToPixel(newX);
                    int py = yToPixel(newY);
                    ptComp->setCentrePosition(px, py);
                    break;
                }
            }

            // Update tension handle positions that depend on this point
            updateTensionHandlePositions();

            // Notify subclass for fluid preview updates
            onPointDragPreview(pointId, newX, newY);

            repaint();
        };

        pc->onPointDeleted = [this](uint32_t pointId) { onPointDeleted(pointId); };

        pc->onHandlesChanged = [this](uint32_t pointId, const CurveHandleData& inHandle,
                                      const CurveHandleData& outHandle) {
            onHandlesChanged(pointId, inHandle, outHandle);
        };

        addAndMakeVisible(pc.get());
        pointComponents_.push_back(std::move(pc));
    }

    // Create tension handles for each curve segment (between consecutive points)
    // Only for Linear curve type - Bezier uses handles, Step has no curve
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];

        // Only create tension handle if this isn't the last point
        // and the curve type is Linear
        if (i < points.size() - 1 && point.curveType == CurveType::Linear) {
            auto th = std::make_unique<CurveTensionHandle>(point.id);
            th->setTension(point.tension);

            // Set slope direction so drag feels intuitive
            const auto& nextPoint = points[i + 1];
            th->setSlopeGoesDown(nextPoint.y < point.y);

            th->onTensionChanged = [this](uint32_t pointId, double tension) {
                // Clear preview state
                tensionPreviewPointId_ = INVALID_CURVE_POINT_ID;
                onTensionChanged(pointId, tension);
            };

            th->onTensionDragPreview = [this](uint32_t pointId, double tension) {
                // Store preview state
                tensionPreviewPointId_ = pointId;
                tensionPreviewValue_ = tension;

                // Update the tension handle position to follow the curve
                const auto& pts = getPoints();
                for (size_t j = 0; j < pts.size() - 1; ++j) {
                    if (pts[j].id == pointId) {
                        const auto& pt1 = pts[j];
                        const auto& pt2 = pts[j + 1];

                        double midX = (pt1.x + pt2.x) / 2.0;
                        double midY = (pt1.y + pt2.y) / 2.0;

                        // Apply tension to get actual curve position at midpoint
                        if (std::abs(tension) > 0.001) {
                            double t = 0.5;
                            double curvedT;
                            if (tension > 0) {
                                curvedT = std::pow(t, 1.0 + tension * 2.0);
                            } else {
                                curvedT = 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
                            }
                            midY = pt1.y + curvedT * (pt2.y - pt1.y);
                        }

                        // Update handle position
                        for (auto& handle : tensionHandles_) {
                            if (handle->getPointId() == pointId) {
                                int px = xToPixel(midX);
                                int py = yToPixel(midY);
                                handle->setCentrePosition(px, py);
                                break;
                            }
                        }
                        break;
                    }
                }

                // Notify subclass for fluid preview updates
                onTensionDragPreview(pointId, tension);

                repaint();
            };

            addAndMakeVisible(th.get());
            tensionHandles_.push_back(std::move(th));
        }
    }

    updatePointPositions();
    syncSelectionState();
}

void CurveEditorBase::updatePointPositions() {
    const auto& points = getPoints();

    for (size_t i = 0; i < pointComponents_.size() && i < points.size(); ++i) {
        const auto& point = points[i];
        int px = xToPixel(point.x);
        int py = yToPixel(point.y);

        pointComponents_[i]->setCentrePosition(px, py);
        pointComponents_[i]->updateFromPoint(point);
    }

    // Position tension handles at the midpoint of each curve segment
    size_t tensionIdx = 0;
    for (size_t i = 0; i < points.size() - 1 && tensionIdx < tensionHandles_.size(); ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        // Only position for Linear curves
        if (p1.curveType == CurveType::Linear) {
            double midX = (p1.x + p2.x) / 2.0;
            double midY = (p1.y + p2.y) / 2.0;

            // Apply tension to get the actual curve position at midpoint
            if (std::abs(p1.tension) > 0.001) {
                double t = 0.5;
                double curvedT;
                if (p1.tension > 0) {
                    curvedT = std::pow(t, 1.0 + p1.tension * 2.0);
                } else {
                    curvedT = 1.0 - std::pow(1.0 - t, 1.0 - p1.tension * 2.0);
                }
                midY = p1.y + curvedT * (p2.y - p1.y);
            }

            int px = xToPixel(midX);
            int py = yToPixel(midY);

            tensionHandles_[tensionIdx]->setCentrePosition(px, py);
            tensionHandles_[tensionIdx]->setTension(p1.tension);
            ++tensionIdx;
        }
    }
}

void CurveEditorBase::updateTensionHandlePositions() {
    const auto& points = getPoints();
    if (points.size() < 2)
        return;

    size_t tensionIdx = 0;
    for (size_t i = 0; i < points.size() - 1 && tensionIdx < tensionHandles_.size(); ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        if (p1.curveType == CurveType::Linear) {
            auto [x1, y1] = getEffectivePosition(p1);
            auto [x2, y2] = getEffectivePosition(p2);

            double midX = (x1 + x2) / 2.0;
            double midY = (y1 + y2) / 2.0;

            // Apply tension to get actual curve position at midpoint
            double tension = p1.tension;
            if (tensionPreviewPointId_ != INVALID_CURVE_POINT_ID &&
                p1.id == tensionPreviewPointId_) {
                tension = tensionPreviewValue_;
            }

            if (std::abs(tension) > 0.001) {
                double t = 0.5;
                double curvedT;
                if (tension > 0) {
                    curvedT = std::pow(t, 1.0 + tension * 2.0);
                } else {
                    curvedT = 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
                }
                midY = y1 + curvedT * (y2 - y1);
            }

            int px = xToPixel(midX);
            int py = yToPixel(midY);

            tensionHandles_[tensionIdx]->setCentrePosition(px, py);
            // Update slope direction in case points were moved
            tensionHandles_[tensionIdx]->setSlopeGoesDown(y2 < y1);
            ++tensionIdx;
        }
    }
}

void CurveEditorBase::createPointsFromDrawingPath() {
    if (drawingPath_.size() < 2)
        return;

    // Simplify path - don't create a point for every pixel
    const int MIN_PIXEL_DISTANCE = 10;

    std::vector<juce::Point<int>> simplifiedPath;
    simplifiedPath.push_back(drawingPath_.front());

    for (size_t i = 1; i < drawingPath_.size(); ++i) {
        const auto& lastAdded = simplifiedPath.back();
        const auto& current = drawingPath_[i];
        int dx = current.x - lastAdded.x;
        int dy = current.y - lastAdded.y;
        int distSq = dx * dx + dy * dy;

        if (distSq >= MIN_PIXEL_DISTANCE * MIN_PIXEL_DISTANCE) {
            simplifiedPath.push_back(current);
        }
    }

    // Always include last point
    if (simplifiedPath.back() != drawingPath_.back()) {
        simplifiedPath.push_back(drawingPath_.back());
    }

    // Create curve points
    CurveType curveType =
        (drawMode_ == CurveDrawMode::Curve) ? CurveType::Bezier : CurveType::Linear;

    for (const auto& pixelPoint : simplifiedPath) {
        double x = pixelToX(pixelPoint.x);
        double y = pixelToY(pixelPoint.y);

        if (snapXToGrid) {
            x = snapXToGrid(x);
        }

        onPointAdded(x, y, curveType);
    }
}

}  // namespace magda
