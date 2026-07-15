#include "AutomationClipComponent.hpp"

#include <algorithm>

#include "../../../core/AutomationCommands.hpp"
#include "../../../core/UndoManager.hpp"
#include "AutomationLaneComponent.hpp"
#include "BinaryData.h"
#include "ui/themes/DarkTheme.hpp"
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
    g.setColour(isSelected_ ? DarkTheme::getColour(DarkTheme::TEXT_BRIGHT)
                            : bgColour.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);

    // Draw mini curve preview
    auto curveBounds = bounds.reduced(4);
    paintMiniCurve(g, curveBounds);

    // Header row: name on the left, loop glyph (same transport loop icon as
    // MIDI/audio clips) on the right when looping.
    auto headerArea = bounds.reduced(4).removeFromTop(14);
    if (clip->looping && headerArea.getWidth() > 30) {
        auto loopArea = headerArea.removeFromRight(14).reduced(1);
        static const auto loopIcon = []() {
            return juce::Drawable::createFromImageData(BinaryData::loop_icon_svg,
                                                       BinaryData::loop_icon_svgSize);
        }();
        if (loopIcon) {
            auto themedIcon = loopIcon->createCopy();
            themedIcon->replaceColour(juce::Colour(0xFFBCBCBC),
                                      DarkTheme::getColour(DarkTheme::TEXT_BRIGHT));
            themedIcon->drawWithin(g, loopArea.toFloat(), juce::RectanglePlacement::centred, 1.0f);
        }
    }
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_BRIGHT));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(clip->name, headerArea, juce::Justification::centredLeft, true);

    // Resize handles visual indication when hovered
    if (isHovered_) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_BRIGHT).withAlpha(0x44 / 255.0f));
        g.fillRect(0, 0, RESIZE_EDGE_WIDTH, getHeight());
        g.fillRect(getWidth() - RESIZE_EDGE_WIDTH, 0, RESIZE_EDGE_WIDTH, getHeight());
    }

    // Loop indicator. Positions computed per-line in double: accumulating a
    // truncated int drifts off the beat grid within a few repetitions.
    if (clip->looping && clip->loopLengthBeats > 0.0 && pixelsPerBeat_ > 0.0) {
        const double stride = clip->loopLengthBeats * pixelsPerBeat_;
        // Below this per-cycle pixel width the markers pack into a barcode —
        // hide them entirely (same threshold as MIDI/audio clip loop markers).
        constexpr double MIN_LOOP_MARKER_PIXEL_WIDTH = 32.0;
        if (stride >= MIN_LOOP_MARKER_PIXEL_WIDTH) {
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_BRIGHT).withAlpha(0xAA / 255.0f));
            for (double x = stride; x < getWidth(); x += stride)
                g.drawVerticalLine(static_cast<int>(std::round(x)), 0.0f,
                                   static_cast<float>(getHeight()));
        }
    }
}

void AutomationClipComponent::paintMiniCurve(juce::Graphics& g, juce::Rectangle<int> bounds) {
    const auto* clip = getClipInfo();
    if (!clip || clip->points.empty() || pixelsPerBeat_ <= 0.0)
        return;

    // X maps beats at the lane's zoom from the clip's left edge — the same
    // mapping as the loop ticks — NOT normalized to the model length: during
    // an edge-resize drag the component width is the preview length, and
    // normalizing would stretch the curve until the mouse-up commit.
    const double spanBeats = getWidth() / pixelsPerBeat_;
    if (spanBeats <= 0.0)
        return;

    // Looped clips repeat one loop cycle across the clip, exactly like
    // playback unrolls them: hold the last value to the cycle end, then a
    // wrap jump back to the cycle-start value.
    const bool looped = clip->looping && clip->loopLengthBeats > 0.0;
    const double cycleBeats = looped ? clip->loopLengthBeats : spanBeats;

    const auto beatToX = [&](double beat) { return static_cast<float>(beat * pixelsPerBeat_); };
    const auto valueToY = [&](double value) {
        return bounds.getBottom() - static_cast<float>(value * bounds.getHeight());
    };

    // The dragged point renders at its live preview coordinates until the
    // mouse-up commit.
    std::vector<AutomationPoint> points = clip->points;
    if (previewPointId_ != INVALID_AUTOMATION_POINT_ID) {
        for (auto& point : points) {
            if (point.id == previewPointId_) {
                point.beatPosition = previewPointBeat_;
                point.value = previewPointValue_;
                break;
            }
        }
        std::sort(points.begin(), points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) {
                      return a.beatPosition < b.beatPosition;
                  });
    }

    // Sample the model's interpolation (bezier / step / tension aware) at
    // ~2px steps: straight point-to-point lines flattened curved segments
    // into triangles. Each loop cycle is sampled separately so the wrap
    // jump stays a crisp vertical edge.
    auto& mgr = AutomationManager::getInstance();
    const double stepBeats = juce::jmax(2.0 / pixelsPerBeat_, 1.0e-3);
    juce::Path curvePath;
    bool pathStarted = false;

    for (double cycleStart = 0.0; cycleStart < spanBeats; cycleStart += cycleBeats) {
        const double cycleEnd = juce::jmin(cycleStart + cycleBeats, spanBeats);
        for (double beat = cycleStart;; beat += stepBeats) {
            const double clamped = juce::jmin(beat, cycleEnd);
            const float x = beatToX(clamped);
            const float y = valueToY(mgr.interpolatePoints(points, clamped - cycleStart));
            if (!pathStarted) {
                curvePath.startNewSubPath(x, y);
                pathStarted = true;
            } else {
                curvePath.lineTo(x, y);
            }
            if (clamped >= cycleEnd)
                break;
        }
        if (!looped)
            break;
    }

    g.saveState();
    g.reduceClipRegion(getLocalBounds());
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_BRIGHT).withAlpha(0xAA / 255.0f));
    g.strokePath(curvePath, juce::PathStrokeType(1.5f));
    g.restoreState();
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
        // The commit lands here; drop any in-flight drag preview.
        previewPointId_ = INVALID_AUTOMATION_POINT_ID;
        repaint();
    }
}

void AutomationClipComponent::automationPointDragPreview(AutomationLaneId laneId,
                                                         AutomationPointId pointId,
                                                         double previewTime, double previewValue) {
    const auto* clip = getClipInfo();
    if (!clip || clip->laneId != laneId)
        return;
    const bool ours = std::any_of(clip->points.begin(), clip->points.end(),
                                  [pointId](const AutomationPoint& p) { return p.id == pointId; });
    if (!ours)
        return;
    previewPointId_ = pointId;
    previewPointBeat_ = previewTime;
    previewPointValue_ = previewValue;
    repaint();
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
