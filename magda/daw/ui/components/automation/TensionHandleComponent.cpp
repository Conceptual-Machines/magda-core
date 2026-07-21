#include "TensionHandleComponent.hpp"

#include "../../themes/DarkTheme.hpp"

namespace magda {

TensionHandleComponent::TensionHandleComponent(AutomationPointId pointId) : pointId_(pointId) {
    setSize(HANDLE_SIZE, HANDLE_SIZE);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void TensionHandleComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    // Background - diamond shape
    juce::Path diamond;
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    float hw = bounds.getWidth() / 2.0f;
    float hh = bounds.getHeight() / 2.0f;

    diamond.startNewSubPath(cx, cy - hh);  // Top
    diamond.lineTo(cx + hw, cy);           // Right
    diamond.lineTo(cx, cy + hh);           // Bottom
    diamond.lineTo(cx - hw, cy);           // Left
    diamond.closeSubPath();

    // Fill color based on state
    juce::Colour fillColour;
    if (isDragging_) {
        fillColour = DarkTheme::getColour(DarkTheme::STATUS_WARNING);
    } else if (isHovered_) {
        fillColour = DarkTheme::getColour(DarkTheme::AUTOMATION_TENSION_HOVER);
    } else {
        fillColour = DarkTheme::getColour(DarkTheme::AUTOMATION_SCALE_TEXT);
    }

    g.setColour(fillColour);
    g.fillPath(diamond);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::AUTOMATION_TEXT));
    g.strokePath(diamond, juce::PathStrokeType(1.0f));
}

void TensionHandleComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        isDragging_ = true;
        dragStartY_ = e.y;
        dragStartTension_ = tension_;
        repaint();
    }
}

void TensionHandleComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_)
        return;

    // Dragging up increases tension (convex/outward), down decreases (concave/inward)
    // 50 pixels of drag = full range
    // Normal: -1 to +1, Shift held: -3 to +3 for extreme squared curves
    int deltaY = dragStartY_ - e.y;
    double deltaTension = static_cast<double>(deltaY) / 50.0;

    double minTension = e.mods.isShiftDown() ? -3.0 : -1.0;
    double maxTension = e.mods.isShiftDown() ? 3.0 : 1.0;
    double newTension = juce::jlimit(minTension, maxTension, dragStartTension_ + deltaTension);

    if (newTension != tension_) {
        tension_ = newTension;

        if (onTensionDragPreview) {
            onTensionDragPreview(pointId_, tension_);
        }

        repaint();
    }
}

void TensionHandleComponent::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isDragging_) {
        isDragging_ = false;

        if (onTensionChanged) {
            onTensionChanged(pointId_, tension_);
        }

        repaint();
    }
}

void TensionHandleComponent::mouseEnter(const juce::MouseEvent& /*e*/) {
    isHovered_ = true;
    repaint();
}

void TensionHandleComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    isHovered_ = false;
    repaint();
}

}  // namespace magda
