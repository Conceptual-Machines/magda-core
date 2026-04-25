#include "AutomationClipComponent.hpp"

#include "AutomationLaneComponent.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

AutomationClipComponent::AutomationClipComponent(AutomationClipId clipId,
                                                 AutomationLaneComponent* parent)
    : clipId_(clipId), parentLane_(parent) {
    setName("AutomationClipComponent");
    setRepaintsOnMouseActivity(true);

    // Register listeners
    AutomationManager::getInstance().addListener(this);
    SelectionManager::getInstance().addListener(this);

    syncSelectionState();
}

AutomationClipComponent::~AutomationClipComponent() {
    AutomationManager::getInstance().removeListener(this);
    SelectionManager::getInstance().removeListener(this);
}

void AutomationClipComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Get clip info
    const auto* clip = getClipInfo();
    if (!clip)
        return;

    // Background color
    juce::Colour bgColour = clip->colour;
    if (isSelected_) {
        bgColour = bgColour.brighter(0.3f);
    } else if (isHovered_) {
        bgColour = bgColour.brighter(0.15f);
    }

    // Draw background with rounded corners
    g.setColour(bgColour.withAlpha(0.8f));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Draw border
    g.setColour(isSelected_ ? juce::Colour(0xFFFFFFFF) : bgColour.darker(0.3f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);

    // Draw mini curve preview
    auto curveBounds = bounds.reduced(4);
    paintMiniCurve(g, curveBounds);

    // Draw clip name
    g.setColour(juce::Colour(0xFFFFFFFF));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    auto textBounds = bounds.reduced(4).removeFromTop(14);
    g.drawText(clip->name, textBounds, juce::Justification::centredLeft, true);

    // Resize handles visual indication when hovered
    if (isHovered_) {
        g.setColour(juce::Colour(0x44FFFFFF));
        g.fillRect(0, 0, RESIZE_EDGE_WIDTH, getHeight());
        g.fillRect(getWidth() - RESIZE_EDGE_WIDTH, 0, RESIZE_EDGE_WIDTH, getHeight());
    }

    // Loop indicator
    if (clip->looping) {
        g.setColour(juce::Colour(0xAAFFFFFF));
        int loopX = static_cast<int>(clip->loopLength * pixelsPerSecond_);
        while (loopX < getWidth()) {
            g.drawVerticalLine(loopX, 0.0f, static_cast<float>(getHeight()));
            loopX += static_cast<int>(clip->loopLength * pixelsPerSecond_);
        }
    }
}

void AutomationClipComponent::paintMiniCurve(juce::Graphics& g, juce::Rectangle<int> bounds) {
    const auto* clip = getClipInfo();
    if (!clip || clip->points.empty())
        return;

    juce::Path curvePath;
    bool pathStarted = false;

    for (const auto& point : clip->points) {
        // Map point to bounds
        float x = bounds.getX() + static_cast<float>(point.time / clip->length) * bounds.getWidth();
        float y = bounds.getBottom() - static_cast<float>(point.value) * bounds.getHeight();

        if (!pathStarted) {
            curvePath.startNewSubPath(x, y);
            pathStarted = true;
        } else {
            curvePath.lineTo(x, y);
        }
    }

    // Draw curve
    g.setColour(juce::Colour(0xAAFFFFFF));
    g.strokePath(curvePath, juce::PathStrokeType(1.5f));
}

void AutomationClipComponent::resized() {
    // Nothing special needed
}

bool AutomationClipComponent::hitTest(int x, int y) {
    return getLocalBounds().contains(x, y);
}

void AutomationClipComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        // Select clip
        if (onClipSelected) {
            onClipSelected(clipId_);
        }

        const auto* clip = getClipInfo();
        if (!clip)
            return;

        // Determine drag mode
        if (isOnLeftEdge(e.x)) {
            dragMode_ = DragMode::ResizeLeft;
        } else if (isOnRightEdge(e.x)) {
            dragMode_ = DragMode::ResizeRight;
        } else {
            dragMode_ = DragMode::Move;
        }

        isDragging_ = true;
        dragStartPos_ = e.getEventRelativeTo(getParentComponent()).getPosition();
        dragStartTime_ = clip->startTime;
        dragStartLength_ = clip->length;
        previewStartTime_ = clip->startTime;
        previewLength_ = clip->length;
    }
}

void AutomationClipComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_ || dragMode_ == DragMode::None)
        return;

    auto parentPos = e.getEventRelativeTo(getParentComponent()).getPosition();
    int deltaX = parentPos.x - dragStartPos_.x;
    double deltaTime = deltaX / pixelsPerSecond_;

    switch (dragMode_) {
        case DragMode::Move: {
            double newStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            if (snapTimeToGrid) {
                newStartTime = snapTimeToGrid(newStartTime);
            }
            previewStartTime_ = newStartTime;

            // Update position visually
            int newX = static_cast<int>(previewStartTime_ * pixelsPerSecond_);
            setBounds(newX, getY(), getWidth(), getHeight());
            break;
        }

        case DragMode::ResizeLeft: {
            double newStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            if (snapTimeToGrid) {
                newStartTime = snapTimeToGrid(newStartTime);
            }
            double endTime = dragStartTime_ + dragStartLength_;
            double newLength = endTime - newStartTime;

            if (newLength > 0.1) {
                previewStartTime_ = newStartTime;
                previewLength_ = newLength;

                int newX = static_cast<int>(previewStartTime_ * pixelsPerSecond_);
                int newWidth = static_cast<int>(previewLength_ * pixelsPerSecond_);
                setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            }
            break;
        }

        case DragMode::ResizeRight: {
            double newLength = juce::jmax(0.1, dragStartLength_ + deltaTime);
            if (snapTimeToGrid) {
                double endTime = snapTimeToGrid(dragStartTime_ + newLength);
                newLength = endTime - dragStartTime_;
            }
            previewLength_ = newLength;

            int newWidth = static_cast<int>(previewLength_ * pixelsPerSecond_);
            setBounds(getX(), getY(), juce::jmax(10, newWidth), getHeight());
            break;
        }

        default:
            break;
    }

    repaint();
}

void AutomationClipComponent::mouseUp(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);

    if (isDragging_) {
        isDragging_ = false;
        auto& manager = AutomationManager::getInstance();

        switch (dragMode_) {
            case DragMode::Move:
                manager.moveClip(clipId_, previewStartTime_);
                break;

            case DragMode::ResizeLeft:
                manager.moveClip(clipId_, previewStartTime_);
                manager.resizeClip(clipId_, previewLength_, false);
                break;

            case DragMode::ResizeRight:
                manager.resizeClip(clipId_, previewLength_, false);
                break;

            default:
                break;
        }

        dragMode_ = DragMode::None;
    }
}

void AutomationClipComponent::mouseEnter(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = true;
    repaint();
}

void AutomationClipComponent::mouseExit(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = false;
    repaint();
}

void AutomationClipComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    if (onClipDoubleClicked) {
        onClipDoubleClicked(clipId_);
    }
}

void AutomationClipComponent::automationClipsChanged(AutomationLaneId laneId) {
    const auto* clip = getClipInfo();
    if (clip && clip->laneId == laneId) {
        repaint();
    }
}

void AutomationClipComponent::selectionTypeChanged(SelectionType newType) {
    juce::ignoreUnused(newType);
    syncSelectionState();
}

void AutomationClipComponent::automationClipSelectionChanged(
    const AutomationClipSelection& selection) {
    juce::ignoreUnused(selection);
    syncSelectionState();
}

void AutomationClipComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        repaint();
    }
}

const AutomationClipInfo* AutomationClipComponent::getClipInfo() const {
    return AutomationManager::getInstance().getClip(clipId_);
}

void AutomationClipComponent::syncSelectionState() {
    auto& selectionManager = SelectionManager::getInstance();

    bool wasSelected = isSelected_;
    isSelected_ = selectionManager.getSelectionType() == SelectionType::AutomationClip &&
                  selectionManager.getAutomationClipSelection().clipId == clipId_;

    if (wasSelected != isSelected_) {
        repaint();
    }
}

}  // namespace magda
