#include "CurvePointComponent.hpp"

#include "CurveEditorBase.hpp"

namespace magda {

CurvePointComponent::CurvePointComponent(uint32_t pointId, CurveEditorBase* parent)
    : pointId_(pointId), parentEditor_(parent) {
    setSize(HIT_SIZE, HIT_SIZE);
    setRepaintsOnMouseActivity(true);
    createHandles();
}

CurvePointComponent::~CurvePointComponent() = default;

void CurvePointComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();

    const float scale = pointScale();
    float radius = (isSelected_ ? POINT_SIZE_SELECTED : POINT_SIZE) * scale / 2.0f;
    float pointSize = radius * 2.0f;
    const auto accent = juce::Colour(0xFFFF8A2A);

    // Draw connection lines to handles if visible
    if (handlesVisible_ && isSelected_) {
        g.setColour(juce::Colour(0x88FFFFFF));

        if (inHandle_ && inHandle_->isVisible()) {
            auto handleCenter = inHandle_->getBounds().getCentre().toFloat();
            auto localHandleCenter =
                getLocalPoint(getParentComponent(), handleCenter.toInt()).toFloat();
            g.drawLine(centerX, centerY, localHandleCenter.x, localHandleCenter.y, 1.0f);
        }

        if (outHandle_ && outHandle_->isVisible()) {
            auto handleCenter = outHandle_->getBounds().getCentre().toFloat();
            auto localHandleCenter =
                getLocalPoint(getParentComponent(), handleCenter.toInt()).toFloat();
            g.drawLine(centerX, centerY, localHandleCenter.x, localHandleCenter.y, 1.0f);
        }
    }

    const bool isHardPoint = point_.curveType == CurveType::HardCorner;
    const auto pointRect =
        juce::Rectangle<float>(centerX - radius, centerY - radius, pointSize, pointSize);

    g.setColour(isHovered_ ? accent.brighter(0.25f) : accent);
    if (isHardPoint)
        g.fillRect(pointRect);
    else
        g.fillEllipse(pointRect);

    if (isSelected_) {
        const auto rr = radius + 2.0f;
        const auto ring = juce::Rectangle<float>(centerX - rr, centerY - rr, rr * 2.0f, rr * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        if (isHardPoint)
            g.drawRect(ring, 1.5f);
        else
            g.drawEllipse(ring, 1.5f);
    }
}

void CurvePointComponent::resized() {
    updateHandlePositions();
}

bool CurvePointComponent::hitTest(int x, int y) {
    auto bounds = getLocalBounds().toFloat();
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();
    float dist = std::sqrt(std::pow(x - centerX, 2) + std::pow(y - centerY, 2));
    // Grow the grab radius with the anchor so the larger external points stay
    // comfortable to hit, but keep a sensible floor for the small inline editor.
    float hitRadius = juce::jmax(POINT_SIZE_SELECTED * pointScale() / 2.0f + 4.0f, 7.0f);
    return dist <= hitRadius;
}

float CurvePointComponent::pointScale() const {
    if (parentEditor_ == nullptr)
        return 1.0f;
    return juce::jlimit(1.0f, 1.7f, static_cast<float>(parentEditor_->getHeight()) / 170.0f);
}

void CurvePointComponent::mouseDown(const juce::MouseEvent& e) {
    if (parentEditor_)
        parentEditor_->grabKeyboardFocus();

    // Right-click is a segment action handled by the editor. Forward it so the
    // parent can resolve the segment under the cursor.
    if (e.mods.isPopupMenu()) {
        isRightClickPending_ = true;
        if (parentEditor_) {
            parentEditor_->mouseDown(e.getEventRelativeTo(parentEditor_));
        }
        return;
    }
    isRightClickPending_ = false;

    if (e.mods.isLeftButtonDown()) {
        // Handle selection
        if (e.mods.isCommandDown() || e.mods.isShiftDown()) {
            // Toggle/add to selection
            if (onPointSelected) {
                onPointSelected(pointId_);
            }
        } else {
            // Normal click - select this point
            if (onPointSelected) {
                onPointSelected(pointId_);
            }
        }

        // Start drag
        isDragging_ = true;
        dragStartPos_ = e.getEventRelativeTo(getParentComponent()).getPosition();
        dragStartX_ = point_.x;
        dragStartY_ = point_.y;
    }
}

void CurvePointComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_ || !parentEditor_)
        return;

    auto parentPos = e.getEventRelativeTo(getParentComponent()).getPosition();
    int deltaXPx = parentPos.x - dragStartPos_.x;
    int deltaYPx = parentPos.y - dragStartPos_.y;

    // Convert pixel delta to x/y using parent's coordinate system
    double pixelsPerX = parentEditor_->getPixelsPerX();
    double pixelsPerY = parentEditor_->getPixelsPerY();

    double newX = dragStartX_ + deltaXPx / pixelsPerX;
    double newY = dragStartY_ - deltaYPx / pixelsPerY;  // Y is inverted

    // Clamp values
    newX = juce::jmax(0.0, newX);
    newY = juce::jlimit(0.0, 1.0, newY);

    if (onPointDragPreview) {
        onPointDragPreview(pointId_, newX, newY);
    }
}

void CurvePointComponent::mouseUp(const juce::MouseEvent& e) {
    if (isRightClickPending_ || e.mods.isPopupMenu()) {
        isRightClickPending_ = false;
        return;
    }

    if (isDragging_) {
        isDragging_ = false;

        if (parentEditor_) {
            auto parentPos = e.getEventRelativeTo(getParentComponent()).getPosition();
            int deltaXPx = parentPos.x - dragStartPos_.x;
            int deltaYPx = parentPos.y - dragStartPos_.y;

            // Only commit a move if the mouse actually moved (> 2px).
            // Skipping no-op moves prevents a rebuildPointComponents() between
            // the two clicks of a double-click, which would destroy this component
            // and prevent mouseDoubleClick from ever firing.
            if (std::abs(deltaXPx) > 2 || std::abs(deltaYPx) > 2) {
                double pixelsPerX = parentEditor_->getPixelsPerX();
                double pixelsPerY = parentEditor_->getPixelsPerY();

                double newX = dragStartX_ + deltaXPx / pixelsPerX;
                double newY = dragStartY_ - deltaYPx / pixelsPerY;

                newX = juce::jmax(0.0, newX);
                newY = juce::jlimit(0.0, 1.0, newY);

                if (onPointMoved) {
                    onPointMoved(pointId_, newX, newY);
                }
            }
        }
    }
}

void CurvePointComponent::mouseEnter(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = true;
    if (onPointHovered)
        onPointHovered(pointId_, true);
    repaint();
}

void CurvePointComponent::mouseExit(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = false;
    if (onPointHovered)
        onPointHovered(pointId_, false);
    repaint();
}

void CurvePointComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    // Double-click to delete point
    if (onPointDeleted) {
        onPointDeleted(pointId_);
    }
}

void CurvePointComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        showHandles(selected && point_.curveType == CurveType::Bezier);
        repaint();
    }
}

void CurvePointComponent::updateFromPoint(const CurvePoint& point) {
    point_ = point;
    updateHandlePositions();
    repaint();
}

void CurvePointComponent::showHandles(bool show) {
    handlesVisible_ = show;

    if (inHandle_)
        inHandle_->setVisible(show);
    if (outHandle_)
        outHandle_->setVisible(show);

    updateHandlePositions();
    repaint();
}

void CurvePointComponent::createHandles() {
    inHandle_ = std::make_unique<CurveBezierHandle>(CurveBezierHandle::HandleType::In, this);
    outHandle_ = std::make_unique<CurveBezierHandle>(CurveBezierHandle::HandleType::Out, this);

    inHandle_->setVisible(false);
    outHandle_->setVisible(false);

    inHandle_->onHandleChanged = [this](CurveBezierHandle::HandleType type, double x, double y,
                                        bool linked) { onHandleChanged(type, x, y, linked); };

    outHandle_->onHandleChanged = [this](CurveBezierHandle::HandleType type, double x, double y,
                                         bool linked) { onHandleChanged(type, x, y, linked); };

    // Handles are added to the parent (curve editor) not this component
}

void CurvePointComponent::updateHandlePositions() {
    if (!parentEditor_ || !handlesVisible_)
        return;

    double pixelsPerX = parentEditor_->getPixelsPerX();
    double pixelsPerY = parentEditor_->getPixelsPerY();

    auto pointCenter = getBounds().getCentre();

    // In handle position
    if (inHandle_) {
        int handleX = pointCenter.x + static_cast<int>(point_.inHandle.x * pixelsPerX);
        int handleY = pointCenter.y - static_cast<int>(point_.inHandle.y * pixelsPerY);
        inHandle_->setCentrePosition(handleX, handleY);
        inHandle_->updateFromHandle(CurveBezierHandle::HandleType::In, point_.inHandle.x,
                                    point_.inHandle.y, point_.inHandle.linked);
    }

    // Out handle position
    if (outHandle_) {
        int handleX = pointCenter.x + static_cast<int>(point_.outHandle.x * pixelsPerX);
        int handleY = pointCenter.y - static_cast<int>(point_.outHandle.y * pixelsPerY);
        outHandle_->setCentrePosition(handleX, handleY);
        outHandle_->updateFromHandle(CurveBezierHandle::HandleType::Out, point_.outHandle.x,
                                     point_.outHandle.y, point_.outHandle.linked);
    }
}

void CurvePointComponent::onHandleChanged(CurveBezierHandle::HandleType type, double x, double y,
                                          bool linked) {
    if (type == CurveBezierHandle::HandleType::In) {
        // Update in handle
        point_.inHandle.x = x;
        point_.inHandle.y = y;
        point_.inHandle.linked = linked;
        // If linked, mirror the out handle
        if (linked) {
            point_.outHandle.x = -x;
            point_.outHandle.y = -y;
        }
    } else {
        // Update out handle
        point_.outHandle.x = x;
        point_.outHandle.y = y;
        point_.outHandle.linked = linked;
        // If linked, mirror the in handle
        if (linked) {
            point_.inHandle.x = -x;
            point_.inHandle.y = -y;
        }
    }

    // Notify with CurveHandleData
    if (onHandlesChanged) {
        onHandlesChanged(pointId_, point_.inHandle, point_.outHandle);
    }
}

}  // namespace magda
