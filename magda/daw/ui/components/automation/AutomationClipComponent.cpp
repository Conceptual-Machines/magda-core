#include "AutomationClipComponent.hpp"

#include "../../../core/AutomationCommands.hpp"
#include "../../../core/UndoManager.hpp"
#include "AutomationLaneComponent.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

AutomationClipComponent::AutomationClipComponent(AutomationClipId clipId) : clipId_(clipId) {
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

    // Translucent body: automation clips must read as a different species
    // from MIDI/audio clips at a glance — the lane shows through them.
    const float fillAlpha = isSelected_ ? 0.5f : (isHovered_ ? 0.42f : 0.35f);
    g.setColour(bgColour.withAlpha(fillAlpha));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Draw border (kept solid so the clip bounds stay crisp)
    g.setColour(isSelected_ ? juce::Colour(0xFFFFFFFF) : bgColour.withAlpha(0.9f));
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

    // Loop indicator. Positions computed per-line in double: accumulating a
    // truncated int drifts off the beat grid within a few repetitions.
    if (clip->looping && clip->loopLengthBeats > 0.0 && pixelsPerBeat_ > 0.0) {
        g.setColour(juce::Colour(0xAAFFFFFF));
        const double stride = clip->loopLengthBeats * pixelsPerBeat_;
        for (double x = stride; x < getWidth(); x += stride)
            g.drawVerticalLine(static_cast<int>(std::round(x)), 0.0f,
                               static_cast<float>(getHeight()));
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
        float x = bounds.getX() +
                  static_cast<float>(point.beatPosition / clip->lengthBeats) * bounds.getWidth();
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
    if (e.mods.isPopupMenu()) {
        if (onClipSelected)
            onClipSelected(clipId_);
        showContextMenu();
        return;
    }

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
        dragStartBeat_ = clip->startBeats;
        dragStartLengthBeats_ = clip->lengthBeats;
        previewStartBeat_ = clip->startBeats;
        previewLengthBeats_ = clip->lengthBeats;
    }
}

void AutomationClipComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_ || dragMode_ == DragMode::None)
        return;

    auto parentPos = e.getEventRelativeTo(getParentComponent()).getPosition();
    int deltaX = parentPos.x - dragStartPos_.x;
    double deltaBeats = deltaX / pixelsPerBeat_;

    switch (dragMode_) {
        case DragMode::Move: {
            double newStartBeat = juce::jmax(0.0, dragStartBeat_ + deltaBeats);
            if (snapBeatToGrid) {
                newStartBeat = snapBeatToGrid(newStartBeat);
            }
            previewStartBeat_ = newStartBeat;

            // Update position visually
            int newX = AutomationLaneComponent::SCALE_LABEL_WIDTH +
                       static_cast<int>(previewStartBeat_ * pixelsPerBeat_);
            setBounds(newX, getY(), getWidth(), getHeight());
            break;
        }

        case DragMode::ResizeLeft: {
            double newStartBeat = juce::jmax(0.0, dragStartBeat_ + deltaBeats);
            if (snapBeatToGrid) {
                newStartBeat = snapBeatToGrid(newStartBeat);
            }
            double endBeat = dragStartBeat_ + dragStartLengthBeats_;
            double newLength = endBeat - newStartBeat;

            if (newLength > 0.1) {
                previewStartBeat_ = newStartBeat;
                previewLengthBeats_ = newLength;

                int newX = AutomationLaneComponent::SCALE_LABEL_WIDTH +
                           static_cast<int>(previewStartBeat_ * pixelsPerBeat_);
                int newWidth = static_cast<int>(previewLengthBeats_ * pixelsPerBeat_);
                setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            }
            break;
        }

        case DragMode::ResizeRight: {
            double newLength = juce::jmax(0.1, dragStartLengthBeats_ + deltaBeats);
            if (snapBeatToGrid) {
                double endBeat = snapBeatToGrid(dragStartBeat_ + newLength);
                newLength = endBeat - dragStartBeat_;
            }
            previewLengthBeats_ = newLength;

            int newWidth = static_cast<int>(previewLengthBeats_ * pixelsPerBeat_);
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
        auto& undoMgr = UndoManager::getInstance();
        const bool moved = previewStartBeat_ != dragStartBeat_;
        const bool resized = previewLengthBeats_ != dragStartLengthBeats_;

        switch (dragMode_) {
            case DragMode::Move:
                if (moved)
                    undoMgr.executeCommand(
                        std::make_unique<MoveAutomationClipCommand>(clipId_, previewStartBeat_));
                break;

            case DragMode::ResizeLeft:
                if (moved || resized) {
                    undoMgr.beginCompoundOperation("Resize Automation Clip");
                    undoMgr.executeCommand(
                        std::make_unique<MoveAutomationClipCommand>(clipId_, previewStartBeat_));
                    undoMgr.executeCommand(std::make_unique<ResizeAutomationClipCommand>(
                        clipId_, previewLengthBeats_, false));
                    undoMgr.endCompoundOperation();
                }
                break;

            case DragMode::ResizeRight:
                if (resized)
                    undoMgr.executeCommand(std::make_unique<ResizeAutomationClipCommand>(
                        clipId_, previewLengthBeats_, false));
                break;

            default:
                break;
        }

        dragMode_ = DragMode::None;
    }
}

void AutomationClipComponent::mouseEnter(const juce::MouseEvent& e) {
    isHovered_ = true;
    updateCursor(e.x);
    repaint();
}

void AutomationClipComponent::mouseMove(const juce::MouseEvent& e) {
    updateCursor(e.x);
}

void AutomationClipComponent::mouseExit(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    isHovered_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void AutomationClipComponent::updateCursor(int x) {
    // Resize cursor over the edge handles, hand elsewhere (drag-to-move).
    if (isOnLeftEdge(x) || isOnRightEdge(x))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void AutomationClipComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    if (onClipDoubleClicked) {
        onClipDoubleClicked(clipId_);
    }
}

void AutomationClipComponent::showContextMenu() {
    const auto* clip = getClipInfo();
    if (!clip)
        return;

    enum MenuItem { EditCurve = 1, Duplicate = 2, ToggleLoop = 3, Delete = 4 };

    juce::PopupMenu menu;
    menu.addItem(EditCurve, "Edit Curve");
    menu.addItem(Duplicate, "Duplicate");
    menu.addItem(ToggleLoop, "Loop", true, clip->looping);
    menu.addSeparator();
    menu.addItem(Delete, "Delete");

    // The component can be rebuilt (or deleted) by the action itself, so the
    // async handler only captures the clip id and the open-editor callback.
    auto clipId = clipId_;
    auto openEditor = onClipDoubleClicked;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this), [clipId, openEditor](int result) {
            switch (result) {
                case EditCurve:
                    if (openEditor)
                        openEditor(clipId);
                    break;
                case Duplicate:
                    juce::MessageManager::callAsync([clipId]() {
                        UndoManager::getInstance().executeCommand(
                            std::make_unique<DuplicateAutomationClipCommand>(clipId));
                    });
                    break;
                case ToggleLoop:
                    juce::MessageManager::callAsync([clipId]() {
                        auto& mgr = AutomationManager::getInstance();
                        if (const auto* c = mgr.getClip(clipId))
                            mgr.setClipLooping(clipId, !c->looping);
                    });
                    break;
                case Delete:
                    juce::MessageManager::callAsync([clipId]() {
                        UndoManager::getInstance().executeCommand(
                            std::make_unique<DeleteAutomationClipCommand>(clipId));
                    });
                    break;
                default:
                    break;
            }
        });
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
