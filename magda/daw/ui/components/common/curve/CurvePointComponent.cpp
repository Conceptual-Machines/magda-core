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

    int pointSize = isSelected_ ? POINT_SIZE_SELECTED : POINT_SIZE;
    float radius = pointSize / 2.0f;

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

    // Point fill color based on state
    juce::Colour fillColour;
    if (isSelected_) {
        fillColour = juce::Colour(0xFFFFFFFF);
    } else if (isHovered_) {
        fillColour = juce::Colour(0xFFCCCCCC);
    } else {
        fillColour = juce::Colour(0xFFAAAAAA);
    }

    // Draw point
    g.setColour(fillColour);
    g.fillEllipse(centerX - radius, centerY - radius, static_cast<float>(pointSize),
                  static_cast<float>(pointSize));

    // Outline
    g.setColour(juce::Colour(0xFF333333));
    g.drawEllipse(centerX - radius, centerY - radius, static_cast<float>(pointSize),
                  static_cast<float>(pointSize), 1.5f);

    // Curve type indicator for bezier
    if (point_.curveType == CurveType::Bezier && isSelected_) {
        g.setColour(juce::Colour(0xFF6688CC));
        g.fillEllipse(centerX - 2, centerY - 2, 4, 4);
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
    return dist <= HIT_SIZE / 2.0f;
}

void CurvePointComponent::mouseDown(const juce::MouseEvent& e) {
    if (parentEditor_)
        parentEditor_->grabKeyboardFocus();

    // Right-click on a point must not be swallowed — forward to the parent
    // curve editor so its context menu (e.g. Simplify Curve) appears whether
    // the user clicked on empty space or directly on a point.
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
