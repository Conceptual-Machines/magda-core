#include "AutomationPointComponent.hpp"

#include "AutomationCurveEditor.hpp"

namespace magda {

AutomationPointComponent::AutomationPointComponent(AutomationPointId pointId,
                                                   AutomationCurveEditor* parent)
    : pointId_(pointId), parentEditor_(parent) {
    setSize(HIT_SIZE, HIT_SIZE);
    setRepaintsOnMouseActivity(true);
    createHandles();
}

AutomationPointComponent::~AutomationPointComponent() = default;

void AutomationPointComponent::paint(juce::Graphics& g) {
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
    g.fillEllipse(centerX - radius, centerY - radius, pointSize, pointSize);

    // Outline
    g.setColour(juce::Colour(0xFF333333));
    g.drawEllipse(centerX - radius, centerY - radius, pointSize, pointSize, 1.5f);

    // Curve type indicator for bezier
    if (point_.curveType == AutomationCurveType::Bezier && isSelected_) {
        g.setColour(juce::Colour(0xFF6688CC));
        g.fillEllipse(centerX - 2, centerY - 2, 4, 4);
    }
}

void AutomationPointComponent::resized() {
    updateHandlePositions();
}

bool AutomationPointComponent::hitTest(int x, int y) {
    auto bounds = getLocalBounds().toFloat();
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();
    float dist = std::sqrt(std::pow(x - centerX, 2) + std::pow(y - centerY, 2));
    return dist <= HIT_SIZE / 2.0f;
}

void AutomationPointComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        // Handle selection
        if (e.mods.isCommandDown()) {
            // Toggle selection (Cmd+click)
            if (onPointSelected) {
                onPointSelected(pointId_);
            }
        } else if (e.mods.isShiftDown()) {
            // Add to selection (Shift+click)
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
        dragStartBeatPosition_ = point_.beatPosition;
        dragStartValue_ = point_.value;
    }
}

void AutomationPointComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_ || !parentEditor_)
        return;

    auto parentPos = e.getEventRelativeTo(getParentComponent()).getPosition();
    int deltaX = parentPos.x - dragStartPos_.x;
    int deltaY = parentPos.y - dragStartPos_.y;

    // Convert pixel delta to beat/value using parent's coordinate system
    double pixelsPerBeat = parentEditor_->getPixelsPerX();
    double pixelsPerValue = parentEditor_->getPixelsPerValue();

    double newBeatPosition = dragStartBeatPosition_ + deltaX / pixelsPerBeat;
    double newValue = dragStartValue_ - deltaY / pixelsPerValue;  // Y is inverted

    // Clamp values
    newBeatPosition = juce::jmax(0.0, newBeatPosition);
    newValue = juce::jlimit(0.0, 1.0, newValue);

    if (onPointDragPreview) {
        onPointDragPreview(pointId_, newBeatPosition, newValue);
    }
}

void AutomationPointComponent::mouseUp(const juce::MouseEvent& e) {
    if (isDragging_) {
        isDragging_ = false;

        if (parentEditor_) {
            auto parentPos = e.getEventRelativeTo(getParentComponent()).getPosition();
            int deltaX = parentPos.x - dragStartPos_.x;
            int deltaY = parentPos.y - dragStartPos_.y;

            double pixelsPerBeat = parentEditor_->getPixelsPerX();
            double pixelsPerValue = parentEditor_->getPixelsPerValue();

            double newBeatPosition = dragStartBeatPosition_ + deltaX / pixelsPerBeat;
            double newValue = dragStartValue_ - deltaY / pixelsPerValue;

            newBeatPosition = juce::jmax(0.0, newBeatPosition);
            newValue = juce::jlimit(0.0, 1.0, newValue);

            if (onPointMoved) {
                onPointMoved(pointId_, newBeatPosition, newValue);
            }
        }
    }
}

void AutomationPointComponent::mouseEnter(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = true;
    repaint();
}

void AutomationPointComponent::mouseExit(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = false;
    repaint();
}

void AutomationPointComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    // Double-click to delete point
    if (onPointDeleted) {
        onPointDeleted(pointId_);
    }
}

void AutomationPointComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        showHandles(selected && point_.curveType == AutomationCurveType::Bezier);
        repaint();
    }
}

void AutomationPointComponent::updateFromPoint(const AutomationPoint& point) {
    point_ = point;
    updateHandlePositions();
    repaint();
}

void AutomationPointComponent::showHandles(bool show) {
    handlesVisible_ = show;

    if (inHandle_)
        inHandle_->setVisible(show);
    if (outHandle_)
        outHandle_->setVisible(show);

    updateHandlePositions();
    repaint();
}

void AutomationPointComponent::createHandles() {
    inHandle_ =
        std::make_unique<BezierHandleComponent>(BezierHandleComponent::HandleType::In, this);
    outHandle_ =
        std::make_unique<BezierHandleComponent>(BezierHandleComponent::HandleType::Out, this);

    inHandle_->setVisible(false);
    outHandle_->setVisible(false);

    inHandle_->onHandleChanged = [this](BezierHandleComponent::HandleType type,
                                        const BezierHandle& handle) {
        onHandleChanged(type, handle);
    };

    outHandle_->onHandleChanged = [this](BezierHandleComponent::HandleType type,
                                         const BezierHandle& handle) {
        onHandleChanged(type, handle);
    };

    // Handles are added to the parent (curve editor) not this component
}

void AutomationPointComponent::updateHandlePositions() {
    if (!parentEditor_ || !handlesVisible_)
        return;

    double pixelsPerBeat = parentEditor_->getPixelsPerX();
    double pixelsPerValue = parentEditor_->getPixelsPerValue();

    auto pointCenter = getBounds().getCentre();

    // In handle position
    if (inHandle_) {
        int handleX = pointCenter.x + static_cast<int>(point_.inHandle.beatOffset * pixelsPerBeat);
        int handleY = pointCenter.y - static_cast<int>(point_.inHandle.value * pixelsPerValue);
        inHandle_->setCentrePosition(handleX, handleY);
        inHandle_->updateFromHandle(point_.inHandle);
    }

    // Out handle position
    if (outHandle_) {
        int handleX = pointCenter.x + static_cast<int>(point_.outHandle.beatOffset * pixelsPerBeat);
        int handleY = pointCenter.y - static_cast<int>(point_.outHandle.value * pixelsPerValue);
        outHandle_->setCentrePosition(handleX, handleY);
        outHandle_->updateFromHandle(point_.outHandle);
    }
}

void AutomationPointComponent::onHandleChanged(BezierHandleComponent::HandleType type,
                                               const BezierHandle& handle) {
    BezierHandle inH = point_.inHandle;
    BezierHandle outH = point_.outHandle;

    if (type == BezierHandleComponent::HandleType::In) {
        inH = handle;
        // If linked, mirror the out handle
        if (inH.linked && outH.linked) {
            outH.beatOffset = -inH.beatOffset;
            outH.value = -inH.value;
        }
    } else {
        outH = handle;
        // If linked, mirror the in handle
        if (inH.linked && outH.linked) {
            inH.beatOffset = -outH.beatOffset;
            inH.value = -outH.value;
        }
    }

    if (onHandlesChanged) {
        onHandlesChanged(pointId_, inH, outH);
    }
}

}  // namespace magda
