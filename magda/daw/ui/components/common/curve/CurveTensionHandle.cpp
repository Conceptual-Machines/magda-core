#include "CurveTensionHandle.hpp"

namespace magda {

CurveTensionHandle::CurveTensionHandle(uint32_t pointId) : pointId_(pointId) {
    setSize(HANDLE_SIZE, HANDLE_SIZE);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void CurveTensionHandle::paint(juce::Graphics& g) {
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
        fillColour = juce::Colour(0xFFFFAA44);  // Orange when dragging
    } else if (isHovered_) {
        fillColour = juce::Colour(0xFFCCAA88);  // Light when hovered
    } else {
        fillColour = juce::Colour(0xFF888888);  // Gray normally
    }

    g.setColour(fillColour);
    g.fillPath(diamond);

    // Border
    g.setColour(juce::Colour(0xFFCCCCCC));
    g.strokePath(diamond, juce::PathStrokeType(1.0f));
}

void CurveTensionHandle::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        isDragging_ = true;
        dragStartY_ = e.y;
        dragStartTension_ = tension_;
        repaint();
    }
}

void CurveTensionHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_)
        return;

    // Dragging up bends curve outward (away from straight line)
    // 50 pixels of drag = full range
    // Normal: -1 to +1, Shift held: -3 to +3 for extreme squared curves
    int deltaY = e.y - dragStartY_;  // Inverted: drag down = positive tension

    // Invert direction when curve goes downward so "up" always means "outward"
    if (slopeGoesDown_) {
        deltaY = -deltaY;
    }

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

void CurveTensionHandle::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isDragging_) {
        isDragging_ = false;

        if (onTensionChanged) {
            onTensionChanged(pointId_, tension_);
        }

        repaint();
    }
}

void CurveTensionHandle::mouseEnter(const juce::MouseEvent& /*e*/) {
    isHovered_ = true;
    repaint();
}

void CurveTensionHandle::mouseExit(const juce::MouseEvent& /*e*/) {
    isHovered_ = false;
    repaint();
}

}  // namespace magda
