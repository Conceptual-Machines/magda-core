#include "CurveEditorBase.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

#include "core/CurveMath.hpp"
#include "core/UndoManager.hpp"
#include "magda/daw/ui/themes/FontManager.hpp"

namespace magda {

CurveEditorBase::CurveEditorBase() {
    setName("CurveEditorBase");
    setWantsKeyboardFocus(true);
}

CurveEditorBase::~CurveEditorBase() = default;

void CurveEditorBase::clearSelection() {
    selectedPointIds_.clear();
    for (auto& pc : pointComponents_) {
        pc->setSelected(false);
    }
    repaint();
}

bool CurveEditorBase::isPointSelected(uint32_t pointId) const {
    return selectedPointIds_.count(pointId) > 0;
}

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

    // Lasso selection rectangle
    if (isLassoActive_ && !lassoRect_.isEmpty()) {
        g.setColour(curveColour_.withAlpha(0.15f));
        g.fillRect(lassoRect_);
        g.setColour(curveColour_.withAlpha(0.6f));
        g.drawRect(lassoRect_, 1);
    }
}

void CurveEditorBase::paintOverChildren(juce::Graphics& g) {
    // Value tooltip for hovered or dragged point
    uint32_t tooltipId =
        (previewPointId_ != INVALID_CURVE_POINT_ID) ? previewPointId_ : hoveredPointId_;
    if (tooltipId == INVALID_CURVE_POINT_ID)
        return;

    // Find the point component
    for (auto& pc : pointComponents_) {
        if (pc->getPointId() != tooltipId)
            continue;

        auto pt = pc->getPoint();
        double yVal = (previewPointId_ != INVALID_CURVE_POINT_ID) ? previewY_ : pt.y;
        juce::String label = formatValueLabel(yVal);

        auto font = FontManager::getInstance().getUIFont(10.0f);
        g.setFont(font);
        int textW = juce::GlyphArrangement::getStringWidthInt(font, label) + 6;
        int textH = 14;

        // Position above the point
        auto pcBounds = pc->getBounds();
        int tx = pcBounds.getCentreX() - textW / 2;
        int ty = pcBounds.getY() - textH - 2;

        // Keep within bounds
        tx = juce::jlimit(0, getWidth() - textW, tx);
        if (ty < 0)
            ty = pcBounds.getBottom() + 2;

        auto tooltipRect = juce::Rectangle<int>(tx, ty, textW, textH);
        g.setColour(juce::Colour(0xDD222222));
        g.fillRoundedRectangle(tooltipRect.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xFFEEEEEE));
        g.drawText(label, tooltipRect, juce::Justification::centred, false);
        break;
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

    // Clear stale preview state if the preview point no longer exists
    if (previewPointId_ != INVALID_CURVE_POINT_ID) {
        bool found = false;
        for (const auto& p : points) {
            if (p.id == previewPointId_) {
                found = true;
                break;
            }
        }
        if (!found) {
            previewPointId_ = INVALID_CURVE_POINT_ID;
        }
    }
    if (tensionPreviewPointId_ != INVALID_CURVE_POINT_ID) {
        bool found = false;
        for (const auto& p : points) {
            if (p.id == tensionPreviewPointId_) {
                found = true;
                break;
            }
        }
        if (!found) {
            tensionPreviewPointId_ = INVALID_CURVE_POINT_ID;
        }
    }

    // Create path for curve
    juce::Path curvePath;
    bool pathStarted = false;

    // Handle edge behavior based on loop mode
    if (shouldLoop()) {
        // For looping (LFO): Edge points are pinned at x=0 and x=1
        // Just start at the first point - no extra wrap segment needed
        if (!points.empty()) {
            auto [firstX, firstY] = getEffectivePosition(points.front());
            float firstPixelX = static_cast<float>(xToPixelF(firstX));
            float firstPixelY = static_cast<float>(yToPixelF(firstY));
            curvePath.startNewSubPath(firstPixelX, firstPixelY);
            pathStarted = true;
        }
    } else {
        // For non-looping (automation): Extend from left edge at first point's value
        if (!points.empty()) {
            auto [firstX, firstY] = getEffectivePosition(points.front());
            float firstPixelX = static_cast<float>(xToPixelF(firstX));
            float firstPixelY = static_cast<float>(yToPixelF(firstY));

            if (firstPixelX > 0.0f) {
                curvePath.startNewSubPath(0.0f, firstPixelY);
                curvePath.lineTo(firstPixelX, firstPixelY);
                pathStarted = true;
            }
        }
    }

    // Draw between points
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        auto [x, y] = getEffectivePosition(p);
        float pixelX = static_cast<float>(xToPixelF(x));
        float pixelY = static_cast<float>(yToPixelF(y));

        if (!pathStarted) {
            curvePath.startNewSubPath(pixelX, pixelY);
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
        // For looping (LFO): Edge points are pinned at x=0 and x=1
        // The curve ends at the last point - no extra segment needed
    } else {
        // For non-looping: Extend to right edge at last point's value
        if (!points.empty()) {
            auto [lastX, lastY] = getEffectivePosition(points.back());
            juce::ignoreUnused(lastX);
            float lastPixelY = static_cast<float>(yToPixelF(lastY));
            float width = static_cast<float>(getWidth());
            curvePath.lineTo(width, lastPixelY);
        }
    }

    // Draw the curve
    g.setColour(curveColour_);
    // Scale the stroke with the editor's size so the small inline preview and the
    // large popped-out editor look consistent (a fixed width reads as too fat on
    // the small one).
    const float strokeW = juce::jlimit(2.0f, 4.5f, static_cast<float>(getHeight()) / 80.0f);
    g.strokePath(curvePath, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    // Optional: fill under curve
    // Use yToPixelF(0) so that subclasses with a vertical edge inset (LFO)
    // get the fill baseline at the padded y=0 position, while the base/
    // automation editor continues to use content.getBottom() (same result
    // because the base yToPixelF(0) == content.getBottom()).
    juce::Path fillPath = curvePath;
    auto content = getContentBounds();
    float fillBaseY = static_cast<float>(yToPixelF(0.0));
    fillPath.lineTo(static_cast<float>(content.getRight()), fillBaseY);
    fillPath.lineTo(static_cast<float>(content.getX()), fillBaseY);
    fillPath.closeSubPath();
    g.setColour(curveColour_.withAlpha(0.13f));
    g.fillPath(fillPath);
}

void CurveEditorBase::renderCurveSegment(juce::Path& path, const CurvePoint& p1,
                                         const CurvePoint& p2, double effectiveTension) {
    auto [x1, y1] = getEffectivePosition(p1);
    auto [x2, y2] = getEffectivePosition(p2);

    // Float-precision pixel coords for rendering (no int truncation)
    float pixelX1 = static_cast<float>(xToPixelF(x1));
    float pixelY1 = static_cast<float>(yToPixelF(y1));
    float pixelX2 = static_cast<float>(xToPixelF(x2));
    float pixelY2 = static_cast<float>(yToPixelF(y2));

    switch (p1.curveType) {
        case CurveType::Linear: {
            constexpr double kHandleEpsilon = 0.000001;
            const bool hasStoredShaper = std::abs(p1.outHandle.x) > kHandleEpsilon ||
                                         std::abs(p1.outHandle.y) > kHandleEpsilon ||
                                         std::abs(p2.inHandle.x) > kHandleEpsilon ||
                                         std::abs(p2.inHandle.y) > kHandleEpsilon ||
                                         shaperPreviewPointId_ == p1.id;
            if (std::abs(effectiveTension) < 0.001 && !hasStoredShaper) {
                // Pure linear
                path.lineTo(pixelX2, pixelY2);
            } else {
                // Sample the shared segment evaluator (core/CurveMath.hpp) so the
                // drawn curve is byte-for-byte what the modulator engine outputs.
                const auto [cx, cy] = getSegmentShaperPosition(p1, p2, effectiveTension);
                juce::ignoreUnused(cx);
                // Scale samples with the segment's on-screen width so a strong
                // (near-vertical) bend stays smooth instead of looking faceted.
                const int kSamples =
                    juce::jlimit(64, 256, static_cast<int>(std::abs(pixelX2 - pixelX1) * 0.5f));
                for (int s = 1; s <= kSamples; ++s) {
                    const double t = static_cast<double>(s) / kSamples;
                    const double xx = x1 + (x2 - x1) * t;
                    const double yy = magda::curvemath::evalSegment(
                        static_cast<float>(y1), static_cast<float>(y2), static_cast<float>(cy),
                        static_cast<float>(effectiveTension), hasStoredShaper,
                        static_cast<float>(t));
                    path.lineTo(static_cast<float>(xToPixelF(xx)),
                                static_cast<float>(yToPixelF(yy)));
                }
            }
            break;
        }

        case CurveType::Bezier: {
            // Control points in float pixel space
            float cp1X = pixelX1 + static_cast<float>(p1.outHandle.x * getPixelsPerX());
            float cp1Y = pixelY1 - static_cast<float>(p1.outHandle.y * getPixelsPerY());
            float cp2X = pixelX2 + static_cast<float>(p2.inHandle.x * getPixelsPerX());
            float cp2Y = pixelY2 - static_cast<float>(p2.inHandle.y * getPixelsPerY());

            path.cubicTo(cp1X, cp1Y, cp2X, cp2Y, pixelX2, pixelY2);
            break;
        }

        case CurveType::Step:
            // Step: horizontal then vertical
            path.lineTo(pixelX2, path.getCurrentPosition().y);
            path.lineTo(pixelX2, pixelY2);
            break;

        case CurveType::HardCorner: {
            auto [midX, midY] = getSegmentShaperPosition(p1, p2, effectiveTension);
            path.lineTo(static_cast<float>(xToPixelF(midX)), static_cast<float>(yToPixelF(midY)));
            path.lineTo(pixelX2, pixelY2);
            break;
        }
    }
}

void CurveEditorBase::paintDrawingPreview(juce::Graphics& g) {
    if ((activeDrawMode_ == CurveDrawMode::Pencil || activeDrawMode_ == CurveDrawMode::Curve) &&
        !drawingPath_.empty()) {
        g.setColour(juce::Colour(0xAAFFFFFF));
        for (size_t i = 1; i < drawingPath_.size(); ++i) {
            g.drawLine(static_cast<float>(drawingPath_[i - 1].x),
                       static_cast<float>(drawingPath_[i - 1].y),
                       static_cast<float>(drawingPath_[i].x), static_cast<float>(drawingPath_[i].y),
                       2.0f);
        }
    }
}

void CurveEditorBase::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();

    // Right-click toggles the segment under the cursor between smooth bend and
    // hard-corner bend. A segment is owned by its left point.
    if (e.mods.isPopupMenu()) {
        isRightClickPending_ = true;
        toggleSegmentHardCorner(findSegmentOwnerAt(pixelToX(e.x)));
        return;
    }
    isRightClickPending_ = false;

    if (e.mods.isLeftButtonDown()) {
        // Resolve effective draw mode from modifier keys:
        //   Cmd/Ctrl → freeform Pencil, Shift → Line stamp, otherwise Select
        if (e.mods.isCommandDown()) {
            activeDrawMode_ = CurveDrawMode::Pencil;
        } else if (e.mods.isShiftDown()) {
            activeDrawMode_ = CurveDrawMode::Line;
        } else {
            activeDrawMode_ = CurveDrawMode::Select;
        }

        switch (activeDrawMode_) {
            case CurveDrawMode::Select:
                // Record click position; lasso starts on drag, point added on click-release
                lassoAnchor_ = e.getPosition();
                isLassoActive_ = false;
                lassoRect_ = {};
                break;

            case CurveDrawMode::Pencil:
                isDrawing_ = true;
                drawingPath_.clear();
                drawingPath_.push_back(e.getPosition());
                break;

            case CurveDrawMode::Line: {
                // Shift+click: stamp a Serum-style step cell spanning one
                // grid division. The cell has a cliff at both edges —
                // achieved by flipping the preceding point to Step inside
                // onStepStamped so the incoming segment holds flat then
                // cliffs into the cell (instead of linearly fading in).
                double x = pixelToX(e.x);
                double y = pixelToY(e.y);
                if (snapYToGrid)
                    y = snapYToGrid(y);
                y = juce::jlimit(0.0, 1.0, y);

                double gridStart = x;
                double gridEnd = x;
                if (snapXToGrid && getGridSpacingX) {
                    gridStart = snapXToGrid(x);
                    gridEnd = gridStart + getGridSpacingX();
                }

                // Find the nearest point strictly before gridStart so the
                // subclass can flip it to Step (left-edge cliff) and so
                // the cell's right edge can return to that point's value
                // (the dip's baseline).
                uint32_t prevPointId = INVALID_CURVE_POINT_ID;
                double prevValue = 0.5;
                const auto& existing = getPoints();
                double bestTime = -std::numeric_limits<double>::infinity();
                for (const auto& p : existing) {
                    if (p.x < gridStart && p.x > bestTime) {
                        bestTime = p.x;
                        prevPointId = p.id;
                        prevValue = p.y;
                    }
                }

                onStepStamped(gridStart, gridEnd, y, prevPointId, prevValue);
                break;
            }

            case CurveDrawMode::Curve:
                isDrawing_ = true;
                drawingPath_.clear();
                drawingPath_.push_back(e.getPosition());
                break;
        }
    }
}

void CurveEditorBase::mouseDrag(const juce::MouseEvent& e) {
    // Select mode: start lasso after a small movement threshold
    if (activeDrawMode_ == CurveDrawMode::Select && !isDrawing_) {
        auto pos = e.getPosition();
        int dx = pos.x - lassoAnchor_.x;
        int dy = pos.y - lassoAnchor_.y;

        if (!isLassoActive_ && (dx * dx + dy * dy) > 16) {
            // Passed threshold — start lasso
            isLassoActive_ = true;
            clearSelection();
        }

        if (isLassoActive_) {
            lassoRect_ = juce::Rectangle<int>(
                std::min(lassoAnchor_.x, pos.x), std::min(lassoAnchor_.y, pos.y),
                std::abs(pos.x - lassoAnchor_.x), std::abs(pos.y - lassoAnchor_.y));
            repaint();
        }
        return;
    }

    if (!isDrawing_)
        return;

    if (activeDrawMode_ == CurveDrawMode::Pencil || activeDrawMode_ == CurveDrawMode::Curve) {
        drawingPath_.push_back(e.getPosition());
        repaint();
    }
}

void CurveEditorBase::mouseUp(const juce::MouseEvent& e) {
    // Right-click release must not reach the point-add path.  e.mods at
    // mouseUp time no longer reflects the released button, so guard via the
    // flag set in mouseDown.
    if (isRightClickPending_) {
        isRightClickPending_ = false;
        return;
    }

    if (activeDrawMode_ == CurveDrawMode::Select && !isDrawing_) {
        if (isLassoActive_) {
            // Finish lasso selection
            isLassoActive_ = false;

            // Gather points whose centres fall within the lasso rectangle
            std::vector<uint32_t> selectedIds;
            for (auto& pc : pointComponents_) {
                auto centre = pc->getBounds().getCentre();
                if (lassoRect_.contains(centre)) {
                    selectedPointIds_.insert(pc->getPointId());
                    pc->setSelected(true);
                    selectedIds.push_back(pc->getPointId());
                }
            }

            if (!selectedIds.empty()) {
                onPointsSelected(selectedIds);
            }

            lassoRect_ = {};
            repaint();
        } else if (addsPointOnSingleClick()) {
            // No drag happened — single click adds a point
            double x = pixelToX(e.x);
            double y = pixelToY(e.y);

            if (snapXToGrid) {
                x = snapXToGrid(x);
            }

            onPointAdded(x, y, CurveType::Linear);
        } else {
            // No drag — a single click just clears the selection. Adding a
            // point is a deliberate double-click (see mouseDoubleClick) so
            // stray clicks don't litter the curve with points.
            clearSelection();
            repaint();
        }
        return;
    }

    if (isDrawing_) {
        isDrawing_ = false;

        switch (activeDrawMode_) {
            case CurveDrawMode::Pencil:
                createPointsFromDrawingPath();
                break;

            case CurveDrawMode::Line:
                break;  // Line is handled on mouseDown (instant stamp)

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
    // Double-clicking empty canvas adds a point — a deliberate gesture so a
    // stray single click doesn't. Double-click on an existing point is
    // intercepted by CurvePointComponent for deletion, so only empty-area
    // double-clicks reach here.
    if (addsPointOnSingleClick() || e.mods.isPopupMenu() ||
        activeDrawMode_ != CurveDrawMode::Select || isDrawing_)
        return;

    double x = pixelToX(e.x);
    double y = pixelToY(e.y);
    if (snapXToGrid)
        x = snapXToGrid(x);
    if (snapYToGrid)
        y = snapYToGrid(y);
    y = juce::jlimit(0.0, 1.0, y);

    onPointAdded(x, y, CurveType::Linear);
}

void CurveEditorBase::modifierKeysChanged(const juce::ModifierKeys& modifiers) {
    updateCursorForModifiers(modifiers);
}

void CurveEditorBase::mouseMove(const juce::MouseEvent& e) {
    // modifierKeysChanged only fires when this component has keyboard focus;
    // hovering back in after focus shifted can leave the cursor stuck on
    // whichever modifier state was last seen. Re-check on every move.
    updateCursorForModifiers(e.mods);
}

void CurveEditorBase::mouseEnter(const juce::MouseEvent& e) {
    updateCursorForModifiers(e.mods);
}

void CurveEditorBase::updateCursorForModifiers(const juce::ModifierKeys& modifiers) {
    if (modifiers.isShiftDown()) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    } else if (modifiers.isCommandDown()) {
        setMouseCursor(juce::MouseCursor::CopyingCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

bool CurveEditorBase::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (!selectedPointIds_.empty()) {
            auto ids = selectedPointIds_;
            onDeleteSelectedPoints(ids);
            selectedPointIds_.clear();
        }
        return true;
    }

    // Cmd+A: select all points
    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        std::vector<uint32_t> allIds;
        for (auto& pc : pointComponents_) {
            selectedPointIds_.insert(pc->getPointId());
            pc->setSelected(true);
            allIds.push_back(pc->getPointId());
        }
        if (!allIds.empty()) {
            onPointsSelected(allIds);
        }
        repaint();
        return true;
    }

    return false;
}

double CurveEditorBase::pixelToY(int py) const {
    auto content = getContentBounds();
    if (content.getHeight() <= 0)
        return 0.5;
    return 1.0 - (static_cast<double>(py - content.getY()) / content.getHeight());
}

int CurveEditorBase::yToPixel(double y) const {
    auto content = getContentBounds();
    return content.getY() + static_cast<int>((1.0 - y) * content.getHeight());
}

// Default float-precision rendering helpers.
// Subclasses override xToPixelF to match their xToPixel math without int truncation.
double CurveEditorBase::xToPixelF(double x) const {
    // Base fallback: same formula as a generic xToPixel would use.
    // Subclasses (LFOCurveEditor, AutomationCurveEditor) override this.
    auto content = getContentBounds();
    return static_cast<double>(content.getX()) + x * getPixelsPerX();
}

double CurveEditorBase::yToPixelF(double y) const {
    auto content = getContentBounds();
    return static_cast<double>(content.getY()) +
           (1.0 - y) * static_cast<double>(content.getHeight());
}

std::pair<double, double> CurveEditorBase::getSegmentShaperPosition(const CurvePoint& p1,
                                                                    const CurvePoint& p2,
                                                                    double effectiveTension) const {
    if (shaperPreviewPointId_ != INVALID_CURVE_POINT_ID && p1.id == shaperPreviewPointId_)
        return {shaperPreviewX_, shaperPreviewY_};

    // Use the effective (preview-aware) endpoint positions, so the segment handle
    // stays on the curve while a point is being dragged. The curve line is drawn
    // from getEffectivePosition too; reading the committed p.x/p.y here instead
    // would leave the handle pinned to the old endpoints and detach it.
    const auto [p1x, p1y] = getEffectivePosition(p1);
    const auto [p2x, p2y] = getEffectivePosition(p2);

    constexpr double kHandleEpsilon = 0.000001;
    const bool hasStoredShaper =
        std::abs(p1.outHandle.x) > kHandleEpsilon || std::abs(p1.outHandle.y) > kHandleEpsilon ||
        std::abs(p2.inHandle.x) > kHandleEpsilon || std::abs(p2.inHandle.y) > kHandleEpsilon;
    if (hasStoredShaper) {
        double sx = p1x + p1.outHandle.x;
        double sy = p1y + p1.outHandle.y;
        // A hard-corner apex is a point ON the curve, so clamp it inside the box.
        // A Linear segment's value is the quadratic CONTROL point, which is allowed
        // outside [0,1] (a strong bend needs the control beyond the range); the
        // rendered curve still stays in range.
        if (p1.curveType == CurveType::HardCorner)
            return {juce::jlimit(p1x, p2x, sx), juce::jlimit(0.0, 1.0, sy)};
        return {juce::jlimit(p1x, p2x, sx), sy};
    }

    double sx = (p1x + p2x) * 0.5;
    double sy = (p1y + p2y) * 0.5;
    if (std::abs(effectiveTension) > 0.001) {
        constexpr double t = 0.5;
        double curvedT;
        if (effectiveTension > 0)
            curvedT = std::pow(t, 1.0 + effectiveTension * 2.0);
        else
            curvedT = 1.0 - std::pow(1.0 - t, 1.0 - effectiveTension * 2.0);
        sy = p1y + curvedT * (p2y - p1y);
    }
    return {sx, juce::jlimit(0.0, 1.0, sy)};
}

std::pair<double, double> CurveEditorBase::getSegmentHandlePosition(const CurvePoint& p1,
                                                                    const CurvePoint& p2,
                                                                    double effectiveTension) const {
    const auto [p1x, p1y] = getEffectivePosition(p1);
    const auto [p2x, p2y] = getEffectivePosition(p2);
    const auto [cx, cy] = getSegmentShaperPosition(p1, p2, effectiveTension);
    // A hard corner's handle is the apex itself (already clamped to the box).
    if (p1.curveType == CurveType::HardCorner)
        return {cx, cy};
    juce::ignoreUnused(cx);
    constexpr double e = 1.0e-6;
    const bool hasStoredShaper = std::abs(p1.outHandle.x) > e || std::abs(p1.outHandle.y) > e ||
                                 std::abs(p2.inHandle.x) > e || std::abs(p2.inHandle.y) > e ||
                                 shaperPreviewPointId_ == p1.id;
    // The handle sits exactly on the rendered curve: its value at the segment
    // midpoint, from the same shared evaluator the engine uses.
    const double midX = 0.5 * (p1x + p2x);
    const double midY = magda::curvemath::evalSegment(
        static_cast<float>(p1y), static_cast<float>(p2y), static_cast<float>(cy),
        static_cast<float>(effectiveTension), hasStoredShaper, 0.5f);
    return {midX, midY};
}

void CurveEditorBase::updateSegmentShaperFromPixel(uint32_t pointId, double pixelX, double pixelY,
                                                   bool isPreview) {
    const auto& points = getPoints();
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i].id != pointId)
            continue;

        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];
        const bool isHardCorner = (p1.curveType == CurveType::HardCorner);

        double cx, cy;              // stored control (Linear) or apex (HardCorner)
        double displayX, displayY;  // where the handle dot sits, always ON the curve

        if (isHardCorner) {
            // Hard corner: the apex is a real point on the curve, so clamp it to
            // the box -- it must never leave the borders. Free in X and Y.
            cx = juce::jlimit(p1.x, p2.x, pixelToX(static_cast<int>(std::round(pixelX))));
            cy = juce::jlimit(0.0, 1.0, pixelToY(static_cast<int>(std::round(pixelY))));
            if (snapXToGrid)
                cx = juce::jlimit(p1.x, p2.x, snapXToGrid(cx));
            if (snapYToGrid)
                cy = juce::jlimit(0.0, 1.0, snapYToGrid(cy));
            displayX = cx;
            displayY = cy;
        } else {
            // Linear: vertical-only bend. The cursor Y is the on-curve target;
            // back-solve the quadratic control so the curve passes through it
            // (B(0.5) = 0.25 P1 + 0.5 C + 0.25 P2). Dragging past the endpoints
            // keeps increasing the bend; the handle stays pinned to the curve.
            juce::ignoreUnused(pixelX);
            const double midX = 0.5 * (p1.x + p2.x);
            const double loY = juce::jmin(p1.y, p2.y);
            const double hiY = juce::jmax(p1.y, p2.y);
            double handleY =
                juce::jlimit(loY - 8.0, hiY + 8.0, pixelToY(static_cast<int>(std::round(pixelY))));
            if (snapYToGrid)
                handleY = juce::jlimit(loY - 8.0, hiY + 8.0, snapYToGrid(handleY));
            cx = midX;
            cy = 2.0 * handleY - 0.5 * (p1.y + p2.y);
            displayX = midX;
            displayY = magda::curvemath::evalSegment(
                static_cast<float>(p1.y), static_cast<float>(p2.y), static_cast<float>(cy),
                static_cast<float>(p1.tension), true, 0.5f);
        }

        shaperPreviewPointId_ = isPreview ? pointId : INVALID_CURVE_POINT_ID;
        shaperPreviewX_ = cx;
        shaperPreviewY_ = cy;

        CurveHandleData p1Out = p1.outHandle;
        p1Out.x = cx - p1.x;
        p1Out.y = cy - p1.y;
        p1Out.linked = true;

        CurveHandleData p2In = p2.inHandle;
        p2In.x = cx - p2.x;
        p2In.y = cy - p2.y;
        p2In.linked = true;

        onSegmentShaperChanged(p1.id, p1.inHandle, p1Out, p2.id, p2In, p2.outHandle, isPreview);

        for (auto& handle : tensionHandles_) {
            if (handle->getPointId() == pointId) {
                handle->setCentrePosition(xToPixel(displayX), yToPixel(displayY));
                break;
            }
        }

        repaint();
        break;
    }
}

uint32_t CurveEditorBase::findSegmentOwnerAt(double x) const {
    const auto& pts = getPoints();
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        if (x >= pts[i].x && x <= pts[i + 1].x)
            return pts[i].id;
    }

    if (pts.size() >= 2)
        return pts[pts.size() - 2].id;

    return INVALID_CURVE_POINT_ID;
}

void CurveEditorBase::toggleSegmentHardCorner(uint32_t pointId) {
    if (pointId == INVALID_CURVE_POINT_ID)
        return;

    // Hard corner is a per-segment property stored on the left point: toggle the
    // segment between a smooth/linear curve and a sharp kink (two straight
    // segments meeting at the draggable apex).
    CurveType currentType = CurveType::Linear;
    for (const auto& point : getPoints()) {
        if (point.id == pointId) {
            currentType = point.curveType;
            break;
        }
    }
    const CurveType newType =
        currentType == CurveType::HardCorner ? CurveType::Linear : CurveType::HardCorner;

    previewPointId_ = INVALID_CURVE_POINT_ID;
    tensionPreviewPointId_ = INVALID_CURVE_POINT_ID;
    shaperPreviewPointId_ = INVALID_CURVE_POINT_ID;
    multiDragStartPositions_.clear();
    multiPreviewPositions_.clear();

    onPointCurveTypeChanged(pointId, newType);
    updatePointPositions();
    updateTensionHandlePositions();
    repaint();
}

void CurveEditorBase::resetSegmentToCenter(uint32_t pointId) {
    if (pointId == INVALID_CURVE_POINT_ID)
        return;
    const auto& points = getPoints();
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i].id != pointId)
            continue;
        // Drive the shaper to the straight-line midpoint, which flattens the
        // segment (and commits it, since isPreview == false).
        const double midX = 0.5 * (points[i].x + points[i + 1].x);
        const double midY = 0.5 * (points[i].y + points[i + 1].y);
        updateSegmentShaperFromPixel(pointId, xToPixel(midX), yToPixel(midY), false);
        break;
    }
}

std::pair<double, double> CurveEditorBase::getEffectivePosition(const CurvePoint& p) const {
    if (previewPointId_ != INVALID_CURVE_POINT_ID) {
        if (p.id == previewPointId_)
            return {previewX_, previewY_};
        // Multi-point drag: check follower preview positions
        auto it = multiPreviewPositions_.find(p.id);
        if (it != multiPreviewPositions_.end())
            return it->second;
    }
    return {p.x, p.y};
}

void CurveEditorBase::rebuildPointComponents() {
    // Clear preview state when structure changes
    previewPointId_ = INVALID_CURVE_POINT_ID;
    tensionPreviewPointId_ = INVALID_CURVE_POINT_ID;

    pointComponents_.clear();
    handleComponents_.clear();
    tensionHandles_.clear();

    const auto& points = getPoints();
    for (const auto& point : points) {
        auto pc = std::make_unique<CurvePointComponent>(point.id, this);
        pc->updateFromPoint(point);

        // Set callbacks — single-click selection with shift toggle
        pc->onPointSelected = [this](uint32_t pointId) {
            // Check if shift is held for additive selection
            bool shiftHeld = juce::ModifierKeys::currentModifiers.isShiftDown();

            if (shiftHeld) {
                // Toggle this point in the selection
                if (selectedPointIds_.count(pointId)) {
                    selectedPointIds_.erase(pointId);
                    for (auto& p : pointComponents_) {
                        if (p->getPointId() == pointId)
                            p->setSelected(false);
                    }
                } else {
                    selectedPointIds_.insert(pointId);
                    for (auto& p : pointComponents_) {
                        if (p->getPointId() == pointId)
                            p->setSelected(true);
                    }
                }
            } else {
                // If clicking a point that's already part of a multi-selection,
                // keep the selection intact so dragging moves all selected points.
                if (!selectedPointIds_.count(pointId) || selectedPointIds_.size() == 1) {
                    selectedPointIds_.clear();
                    for (auto& p : pointComponents_) {
                        p->setSelected(false);
                    }
                    selectedPointIds_.insert(pointId);
                    for (auto& p : pointComponents_) {
                        if (p->getPointId() == pointId)
                            p->setSelected(true);
                    }
                }
            }

            onPointSelected(pointId);
        };

        pc->onPointMoved = [this](uint32_t pointId, double newX, double newY) {
            // Clear preview state - drag is complete
            previewPointId_ = INVALID_CURVE_POINT_ID;

            // Snap X/Y if enabled
            if (snapXToGrid)
                newX = snapXToGrid(newX);
            if (snapYToGrid)
                newY = snapYToGrid(newY);
            constrainPointPosition(pointId, newX, newY);

            if (selectedPointIds_.size() > 1 && multiDragStartPositions_.count(pointId)) {
                // Multi-point drag commit: compute delta from lead point's start,
                // apply to all selected points, commit as a batch.
                const auto& leadStart = multiDragStartPositions_.at(pointId);
                double deltaX = newX - leadStart.first;
                double deltaY = newY - leadStart.second;

                std::map<uint32_t, std::pair<double, double>> finalPositions;
                for (const auto& [pid, startPos] : multiDragStartPositions_) {
                    double fx = std::max(0.0, startPos.first + deltaX);
                    double fy = juce::jlimit(0.0, 1.0, startPos.second + deltaY);
                    if (pid != pointId) {
                        if (snapXToGrid)
                            fx = snapXToGrid(fx);
                        if (snapYToGrid)
                            fy = snapYToGrid(fy);
                        constrainPointPosition(pid, fx, fy);
                    } else {
                        fx = newX;
                        fy = newY;
                    }
                    finalPositions[pid] = {fx, fy};
                }
                onSelectedPointsMoved(finalPositions);
            } else {
                onPointMoved(pointId, newX, newY);
            }

            multiDragStartPositions_.clear();
            multiPreviewPositions_.clear();
        };

        pc->onPointDragPreview = [this](uint32_t pointId, double newX, double newY) {
            // Snap X/Y if enabled
            if (snapXToGrid)
                newX = snapXToGrid(newX);
            if (snapYToGrid)
                newY = snapYToGrid(newY);
            constrainPointPosition(pointId, newX, newY);

            // On first call for this drag, snapshot start positions of all
            // selected points so we can move them by the same delta.
            if (previewPointId_ != pointId) {
                multiDragStartPositions_.clear();
                multiPreviewPositions_.clear();
                for (const auto& p : getPoints()) {
                    if (selectedPointIds_.count(p.id)) {
                        multiDragStartPositions_[p.id] = {p.x, p.y};
                    }
                }
            }

            // Update lead point preview
            previewPointId_ = pointId;
            previewX_ = newX;
            previewY_ = newY;

            // If multiple points selected, move followers by the same delta
            if (selectedPointIds_.size() > 1 && multiDragStartPositions_.count(pointId)) {
                const auto& leadStart = multiDragStartPositions_.at(pointId);
                double deltaX = newX - leadStart.first;
                double deltaY = newY - leadStart.second;

                multiPreviewPositions_.clear();
                for (auto& ptComp : pointComponents_) {
                    uint32_t pid = ptComp->getPointId();
                    if (!selectedPointIds_.count(pid))
                        continue;
                    double fx, fy;
                    if (pid == pointId) {
                        fx = newX;
                        fy = newY;
                    } else if (multiDragStartPositions_.count(pid)) {
                        const auto& s = multiDragStartPositions_.at(pid);
                        fx = std::max(0.0, s.first + deltaX);
                        fy = juce::jlimit(0.0, 1.0, s.second + deltaY);
                    } else {
                        continue;
                    }
                    multiPreviewPositions_[pid] = {fx, fy};
                    ptComp->setCentrePosition(xToPixel(fx), yToPixel(fy));
                }
            } else {
                // Single point: just reposition that one component
                for (auto& ptComp : pointComponents_) {
                    if (ptComp->getPointId() == pointId) {
                        ptComp->setCentrePosition(xToPixel(newX), yToPixel(newY));
                        break;
                    }
                }
            }

            updateTensionHandlePositions();
            onPointDragPreview(pointId, newX, newY);
            repaint();
        };

        pc->onPointDeleted = [this](uint32_t pointId) { onPointDeleted(pointId); };

        pc->onHandlesChanged = [this](uint32_t pointId, const CurveHandleData& inHandle,
                                      const CurveHandleData& outHandle) {
            onHandlesChanged(pointId, inHandle, outHandle);
        };

        pc->onPointHovered = [this](uint32_t pointId, bool hovered) {
            hoveredPointId_ = hovered ? pointId : INVALID_CURVE_POINT_ID;
            repaint();
        };

        addAndMakeVisible(pc.get());
        pointComponents_.push_back(std::move(pc));
    }

    // Create tension handles for bendable curve segments.
    // Bezier uses handles; Step has no bend control.
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];

        if (i < points.size() - 1 &&
            (point.curveType == CurveType::Linear || point.curveType == CurveType::HardCorner)) {
            auto th = std::make_unique<CurveTensionHandle>(point.id);
            th->onRightClick = [this](uint32_t pointId) { toggleSegmentHardCorner(pointId); };
            th->onReset = [this](uint32_t pointId) { resetSegmentToCenter(pointId); };

            th->onShaperDragPreview = [this](uint32_t pointId, double pixelX, double pixelY) {
                updateSegmentShaperFromPixel(pointId, pixelX, pixelY, true);
            };

            th->onShaperChanged = [this](uint32_t pointId, double pixelX, double pixelY) {
                updateSegmentShaperFromPixel(pointId, pixelX, pixelY, false);
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
    constexpr int MIN_SEGMENT_PIXELS = 30;  // Hide handle if segment narrower than this
    size_t tensionIdx = 0;
    for (size_t i = 0; i < points.size() - 1 && tensionIdx < tensionHandles_.size(); ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        // Only position for Linear curves
        if (p1.curveType == CurveType::Linear || p1.curveType == CurveType::HardCorner) {
            int segPixels = xToPixel(p2.x) - xToPixel(p1.x);
            bool hasRoom = segPixels >= MIN_SEGMENT_PIXELS;
            tensionHandles_[tensionIdx]->setVisible(hasRoom);
            tensionHandles_[tensionIdx]->setHardCorner(p1.curveType == CurveType::HardCorner);

            if (hasRoom) {
                auto [midX, midY] = getSegmentHandlePosition(p1, p2, p1.tension);

                int px = xToPixel(midX);
                int py = yToPixel(midY);

                tensionHandles_[tensionIdx]->setCentrePosition(px, py);
            }
            ++tensionIdx;
        }
    }
}

void CurveEditorBase::updateTensionHandlePositions() {
    const auto& points = getPoints();
    if (points.size() < 2)
        return;

    constexpr int MIN_SEGMENT_PIXELS = 30;
    size_t tensionIdx = 0;
    for (size_t i = 0; i < points.size() - 1 && tensionIdx < tensionHandles_.size(); ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        if (p1.curveType == CurveType::Linear || p1.curveType == CurveType::HardCorner) {
            auto [x1, y1] = getEffectivePosition(p1);
            auto [x2, y2] = getEffectivePosition(p2);

            int segPixels = xToPixel(x2) - xToPixel(x1);
            bool hasRoom = segPixels >= MIN_SEGMENT_PIXELS;
            tensionHandles_[tensionIdx]->setVisible(hasRoom);
            tensionHandles_[tensionIdx]->setHardCorner(p1.curveType == CurveType::HardCorner);

            if (hasRoom) {
                double tension = p1.tension;
                if (tensionPreviewPointId_ != INVALID_CURVE_POINT_ID &&
                    p1.id == tensionPreviewPointId_) {
                    tension = tensionPreviewValue_;
                }

                juce::ignoreUnused(x1, y1, x2, y2);
                auto [midX, midY] = getSegmentHandlePosition(p1, p2, tension);

                int px = xToPixel(midX);
                int py = yToPixel(midY);

                tensionHandles_[tensionIdx]->setCentrePosition(px, py);
            }
            ++tensionIdx;
        }
    }
}

void CurveEditorBase::syncSelectionState() {
    for (auto& pc : pointComponents_) {
        pc->setSelected(selectedPointIds_.count(pc->getPointId()) > 0);
    }
}

void CurveEditorBase::onStepStamped(double gridStart, double gridEnd, double y,
                                    uint32_t prevPointId, double prevValue) {
    // Default behaviour: add the cell's left edge at (gridStart, y) and,
    // if we have a baseline, a recovery point at (gridEnd, prevValue) so
    // the cell reads as a dip back to the previous value. The recovery
    // point is Linear so downstream segments don't mutate into
    // hold-then-cliff. Subclasses that support undo compound ops (e.g.
    // AutomationCurveEditor) should override to also flip prevPointId's
    // curveType to Step so the cell's left edge is a cliff instead of a
    // linear fade.
    onPointAdded(gridStart, y, CurveType::Step);
    if (prevPointId != INVALID_CURVE_POINT_ID && gridEnd > gridStart)
        onPointAdded(gridEnd, prevValue, CurveType::Linear);
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
        if (snapYToGrid) {
            y = snapYToGrid(y);
        }

        onPointAdded(x, y, curveType);
    }
}

}  // namespace magda
