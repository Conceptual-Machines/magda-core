#include "CurveBezierHandle.hpp"

#include "CurvePointComponent.hpp"
#include "magda/daw/ui/themes/DarkTheme.hpp"

namespace magda {

CurveBezierHandle::CurveBezierHandle(HandleType type, CurvePointComponent* parentPoint)
    : handleType_(type), parentPoint_(parentPoint) {
    setSize(HIT_SIZE, HIT_SIZE);
    setRepaintsOnMouseActivity(true);
}

CurveBezierHandle::~CurveBezierHandle() = default;

void CurveBezierHandle::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();
    float radius = HANDLE_SIZE / 2.0f;

    // Handle fill - lighter when hovered
    juce::Colour handleColour = isHovered_ ? DarkTheme::getColour(DarkTheme::AUTOMATION_POINT)
                                           : DarkTheme::getColour(DarkTheme::AUTOMATION_SCALE_TEXT);

    if (isDragging_) {
        handleColour = DarkTheme::getColour(DarkTheme::TEXT_BRIGHT);
    }

    g.setColour(handleColour);
    g.fillEllipse(centerX - radius, centerY - radius, HANDLE_SIZE, HANDLE_SIZE);

    // Handle outline
    g.setColour(DarkTheme::getColour(DarkTheme::AUTOMATION_DIVIDER_LIGHT));
    g.drawEllipse(centerX - radius, centerY - radius, HANDLE_SIZE, HANDLE_SIZE, 1.0f);
}

void CurveBezierHandle::resized() {
    // Component is centered on the handle position
}

void CurveBezierHandle::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        isDragging_ = true;
        dragStartPos_ = e.getPosition();
        dragStartX_ = handleX_;
        dragStartY_ = handleY_;
        repaint();
    }
}

void CurveBezierHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_ || !parentPoint_)
        return;

    // Calculate delta in parent coordinates
    auto parentComponent = getParentComponent();
    if (!parentComponent)
        return;

    auto localPos = e.getPosition();
    int deltaX = localPos.x - dragStartPos_.x;
    int deltaY = localPos.y - dragStartPos_.y;

    // Convert pixel delta to x/y delta
    // This requires knowledge of the curve editor's zoom/scale
    // For now, use simple conversion (will be refined by parent)
    double xScale = 0.01;  // Units per pixel
    double yScale = 0.01;  // Value units per pixel

    double newX = dragStartX_ + deltaX * xScale;
    double newY = dragStartY_ - deltaY * yScale;  // Y is inverted

    handleX_ = newX;
    handleY_ = newY;

    if (onHandleDragPreview) {
        onHandleDragPreview(handleType_, newX, newY, linked_);
    }

    repaint();
}

void CurveBezierHandle::mouseUp(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);

    if (isDragging_) {
        isDragging_ = false;

        if (onHandleChanged) {
            onHandleChanged(handleType_, handleX_, handleY_, linked_);
        }

        repaint();
    }
}

void CurveBezierHandle::mouseEnter(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = true;
    repaint();
}

void CurveBezierHandle::mouseExit(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = false;
    repaint();
}

void CurveBezierHandle::updateFromHandle(const CurveBezierHandle::HandleType /*type*/, double x,
                                         double y, bool linked) {
    handleX_ = x;
    handleY_ = y;
    linked_ = linked;
    repaint();
}

}  // namespace magda
