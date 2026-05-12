#include "BezierHandleComponent.hpp"

#include "AutomationPointComponent.hpp"

namespace magda {

BezierHandleComponent::BezierHandleComponent(HandleType type, AutomationPointComponent* parentPoint)
    : handleType_(type), parentPoint_(parentPoint) {
    setSize(HIT_SIZE, HIT_SIZE);
    setRepaintsOnMouseActivity(true);
}

BezierHandleComponent::~BezierHandleComponent() = default;

void BezierHandleComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    float centerX = bounds.getCentreX();
    float centerY = bounds.getCentreY();
    float radius = HANDLE_SIZE / 2.0f;

    // Handle fill - lighter when hovered
    juce::Colour handleColour = isHovered_ ? juce::Colour(0xFFAAAAAA) : juce::Colour(0xFF888888);

    if (isDragging_) {
        handleColour = juce::Colour(0xFFFFFFFF);
    }

    g.setColour(handleColour);
    g.fillEllipse(centerX - radius, centerY - radius, HANDLE_SIZE, HANDLE_SIZE);

    // Handle outline
    g.setColour(juce::Colour(0xFF444444));
    g.drawEllipse(centerX - radius, centerY - radius, HANDLE_SIZE, HANDLE_SIZE, 1.0f);
}

void BezierHandleComponent::resized() {
    // Component is centered on the handle position
}

void BezierHandleComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        isDragging_ = true;
        dragStartPos_ = e.getPosition();
        dragStartHandle_ = handle_;
        repaint();
    }
}

void BezierHandleComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_ || !parentPoint_)
        return;

    // Calculate delta in parent coordinates
    auto parentComponent = getParentComponent();
    if (!parentComponent)
        return;

    auto localPos = e.getPosition();
    int deltaX = localPos.x - dragStartPos_.x;
    int deltaY = localPos.y - dragStartPos_.y;

    // Convert pixel delta to beat/value delta
    // This requires knowledge of the curve editor's zoom/scale
    // For now, use simple conversion (will be refined by parent)
    double beatScale = 0.01;   // Beats per pixel
    double valueScale = 0.01;  // Value units per pixel

    BezierHandle newHandle = dragStartHandle_;
    newHandle.beatOffset = dragStartHandle_.beatOffset + deltaX * beatScale;
    newHandle.value = dragStartHandle_.value - deltaY * valueScale;  // Y is inverted

    handle_ = newHandle;

    if (onHandleDragPreview) {
        onHandleDragPreview(handleType_, newHandle);
    }

    repaint();
}

void BezierHandleComponent::mouseUp(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);

    if (isDragging_) {
        isDragging_ = false;

        if (onHandleChanged) {
            onHandleChanged(handleType_, handle_);
        }

        repaint();
    }
}

void BezierHandleComponent::mouseEnter(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = true;
    repaint();
}

void BezierHandleComponent::mouseExit(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = false;
    repaint();
}

void BezierHandleComponent::updateFromHandle(const BezierHandle& handle) {
    handle_ = handle;
    repaint();
}

void BezierHandleComponent::setVisible(bool visible) {
    Component::setVisible(visible);
}

}  // namespace magda
