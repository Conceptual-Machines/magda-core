#include "CurveTensionHandle.hpp"

namespace magda {

CurveTensionHandle::CurveTensionHandle(uint32_t pointId) : pointId_(pointId) {
    setSize(HANDLE_SIZE, HANDLE_SIZE);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void CurveTensionHandle::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.25f);
    const auto accent = juce::Colour(0xFFFF8A2A);

    const float lineW = isDragging_ || isHovered_ ? 1.4f : 1.1f;
    const auto strokeColour =
        isDragging_ ? accent : (isHovered_ ? juce::Colours::white : juce::Colour(0xFFE6E6E6));

    g.setColour(juce::Colour(0xFF141414));
    if (isHardCorner_) {
        // Square to signal a hard-corner segment (minimal rounding so it reads
        // clearly as a square, not a circle).
        constexpr float corner = 0.5f;
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(strokeColour);
        g.drawRoundedRectangle(bounds, corner, lineW);
    } else {
        g.fillEllipse(bounds);
        g.setColour(strokeColour);
        g.drawEllipse(bounds, lineW);
    }
}

void CurveTensionHandle::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onRightClick)
            onRightClick(pointId_);
        return;
    }
    if (e.mods.isLeftButtonDown()) {
        isDragging_ = true;
        dragStartTension_ = tension_;
        auto parentPos = e.getEventRelativeTo(getParentComponent()).position;
        dragOffsetX_ = static_cast<double>(getBounds().getCentreX()) - parentPos.x;
        dragOffsetY_ = static_cast<double>(getBounds().getCentreY()) - parentPos.y;
        lastShaperX_ = static_cast<double>(getBounds().getCentreX());
        lastShaperY_ = static_cast<double>(getBounds().getCentreY());
        repaint();
    }
}

void CurveTensionHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_)
        return;

    auto parentPos = e.getEventRelativeTo(getParentComponent()).position;
    double shaperX = parentPos.x + dragOffsetX_;
    double shaperY = parentPos.y + dragOffsetY_;
    lastShaperX_ = shaperX;
    lastShaperY_ = shaperY;

    if (onShaperDragPreview)
        onShaperDragPreview(pointId_, shaperX, shaperY);
    else if (onTensionDragPreview)
        onTensionDragPreview(pointId_, dragStartTension_);

    repaint();
}

void CurveTensionHandle::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isDragging_) {
        isDragging_ = false;

        if (!onShaperChanged && onTensionChanged) {
            onTensionChanged(pointId_, tension_);
        }
        if (onShaperChanged) {
            // Commit the last cursor-driven position, not the handle's clamped
            // on-curve bounds, so a past-the-border bend persists on release.
            onShaperChanged(pointId_, lastShaperX_, lastShaperY_);
        }

        repaint();
    }
}

void CurveTensionHandle::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown() && onReset) {
        isDragging_ = false;  // a double-click is not a drag
        onReset(pointId_);
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
