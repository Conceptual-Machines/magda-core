#include "NoteComponent.hpp"

#include "NoteGridHost.hpp"

namespace magda {

NoteComponent::NoteComponent(size_t noteIndex, NoteGridHost* parent, ClipId sourceClipId)
    : noteIndex_(noteIndex), sourceClipId_(sourceClipId), parentGrid_(parent) {
    setName("NoteComponent");
}

void NoteComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    if (ghost_) {
        // Ghost note: slightly dimmed fill with subtle border
        g.setColour(colour_.withAlpha(0.35f));
        g.fillRoundedRectangle(bounds, CORNER_RADIUS);
        g.setColour(colour_.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), CORNER_RADIUS, 1.0f);
        return;
    }

    // Background fill — gradient based on velocity
    float velocityRatio = velocity_ / 127.0f;
    auto baseColour = isSelected_ ? colour_.brighter(0.3f) : colour_;
    auto fillColour = baseColour.darker(1.0f - velocityRatio);
    g.setColour(fillColour);
    g.fillRoundedRectangle(bounds, CORNER_RADIUS);

    // Border
    g.setColour(isSelected_ ? juce::Colours::white : fillColour.brighter(0.4f));
    float strokeWidth = isSelected_ ? 2.0f : 1.0f;
    g.drawRoundedRectangle(bounds.reduced(0.5f), CORNER_RADIUS, strokeWidth);

    // Resize handle highlights
    if (isSelected_) {
        auto handleColour = juce::Colours::white.withAlpha(0.4f);
        if (hoverLeftEdge_) {
            g.setColour(handleColour);
            g.fillRect(0, 0, RESIZE_HANDLE_WIDTH, getHeight());
        }
        if (hoverRightEdge_) {
            g.setColour(handleColour);
            g.fillRect(getWidth() - RESIZE_HANDLE_WIDTH, 0, RESIZE_HANDLE_WIDTH, getHeight());
        }
    }
}

void NoteComponent::resized() {
    // Nothing to do - bounds are set by parent
}

void NoteComponent::mouseDown(const juce::MouseEvent& e) {
    // Handle Cmd+click for toggle selection (additive)
    if (e.mods.isCommandDown()) {
        setSelected(!isSelected_);
        if (onNoteSelected) {
            onNoteSelected(noteIndex_, true);
        }
        dragMode_ = DragMode::None;
        return;
    }

    // Single click - select this note
    if (!isSelected_) {
        setSelected(true);
        // Clicking an unselected note: deselect others immediately
        if (onNoteSelected) {
            onNoteSelected(noteIndex_, false);
        }
        deferredDeselect_ = false;
    } else {
        // Already selected: defer deselect-others to mouseUp so multi-drag works
        deferredDeselect_ = true;
    }

    // Store drag start info (in grid-relative coordinates)
    if (parentGrid_) {
        auto screenPos = e.getScreenPosition();
        dragStartPos_ = screenPos - parentGrid_->getGridScreenPosition();
    } else {
        dragStartPos_ = e.getPosition();
    }
    dragStartBeat_ = startBeat_;
    dragStartLength_ = lengthBeats_;
    dragStartNoteNumber_ = noteNumber_;

    // Initialize preview state
    previewStartBeat_ = startBeat_;
    previewLengthBeats_ = lengthBeats_;
    previewNoteNumber_ = noteNumber_;
    isDragging_ = false;

    // Determine drag mode based on click position
    if (isOnLeftEdge(e.x)) {
        dragMode_ = DragMode::ResizeLeft;
    } else if (isOnRightEdge(e.x)) {
        dragMode_ = DragMode::ResizeRight;
    } else {
        dragMode_ = DragMode::Move;
        // Shift+drag starts a copy operation
        isCopyDrag_ = e.mods.isShiftDown();
    }

    repaint();
}

void NoteComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::None || !parentGrid_) {
        return;
    }

    isDragging_ = true;

    // Get pixels per beat and note height from parent
    double pixelsPerBeat = parentGrid_->getPixelsPerBeat();
    int noteHeight = parentGrid_->getNoteHeight();

    if (pixelsPerBeat <= 0 || noteHeight <= 0) {
        return;
    }

    // Calculate delta in parent coordinates
    // Use Desktop mouse position to avoid component-relative constraints
    auto& desktop = juce::Desktop::getInstance();
    auto absoluteMousePos = desktop.getMainMouseSource().getScreenPosition();
    auto gridScreenPos = parentGrid_->getGridScreenPosition();
    auto parentPos = absoluteMousePos.toInt() - gridScreenPos;

    int deltaX = parentPos.x - dragStartPos_.x;
    int deltaY = parentPos.y - dragStartPos_.y;

    double deltaBeat = deltaX / pixelsPerBeat;
    int deltaNote = -deltaY / noteHeight;  // Negative because Y increases downward

    switch (dragMode_) {
        case DragMode::Move: {
            double rawStartBeat = dragStartBeat_ + deltaBeat;
            int rawNoteNumber = juce::jlimit(0, 127, dragStartNoteNumber_ + deltaNote);

            // Apply grid snap if available
            if (snapBeatToGrid) {
                rawStartBeat = snapBeatToGrid(rawStartBeat);
            }

            previewStartBeat_ = rawStartBeat;
            previewNoteNumber_ = rawNoteNumber;

            // For copy drag, keep original in place and show ghost at destination
            if (isCopyDrag_) {
                parentGrid_->setCopyDragPreview(previewStartBeat_, previewNoteNumber_,
                                                dragStartLength_, colour_, true, noteIndex_);
            } else {
                parentGrid_->updateNotePosition(this, previewStartBeat_, previewNoteNumber_,
                                                dragStartLength_);
            }

            // Notify listeners of drag preview
            if (onNoteDragging) {
                onNoteDragging(noteIndex_, previewStartBeat_, true);
            }
            break;
        }

        case DragMode::ResizeLeft: {
            double rawStartBeat = juce::jmax(0.0, dragStartBeat_ + deltaBeat);
            double endBeat = dragStartBeat_ + dragStartLength_;

            // Apply grid snap
            if (snapBeatToGrid) {
                rawStartBeat = snapBeatToGrid(rawStartBeat);
            }

            // Ensure minimum length
            double minLength = 1.0 / 16.0;  // 1/16th note minimum
            rawStartBeat = juce::jmin(rawStartBeat, endBeat - minLength);
            double newLength = endBeat - rawStartBeat;

            previewStartBeat_ = rawStartBeat;
            previewLengthBeats_ = newLength;

            // Update visual position
            parentGrid_->updateNotePosition(this, previewStartBeat_, noteNumber_,
                                            previewLengthBeats_);
            break;
        }

        case DragMode::ResizeRight: {
            double rawEndBeat = dragStartBeat_ + dragStartLength_ + deltaBeat;

            // Apply grid snap to end beat
            if (snapBeatToGrid) {
                rawEndBeat = snapBeatToGrid(rawEndBeat);
            }

            // Ensure minimum length
            double minLength = 1.0 / 16.0;
            double newLength = juce::jmax(minLength, rawEndBeat - dragStartBeat_);
            previewLengthBeats_ = newLength;

            // Update visual position
            parentGrid_->updateNotePosition(this, dragStartBeat_, noteNumber_, previewLengthBeats_);
            break;
        }

        default:
            break;
    }
}

void NoteComponent::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isDragging_ && dragMode_ != DragMode::None) {
        // Commit the change via callback
        switch (dragMode_) {
            case DragMode::Move:
                if (isCopyDrag_) {
                    // Copy: add a new note at destination, leave original in place
                    if (onNoteCopied) {
                        onNoteCopied(noteIndex_, previewStartBeat_, previewNoteNumber_);
                    }
                } else {
                    if (onNoteMoved) {
                        onNoteMoved(noteIndex_, previewStartBeat_, previewNoteNumber_);
                    }
                }
                break;

            case DragMode::ResizeLeft:
                // Resizing from left changes both start and length
                if (onNoteMoved) {
                    onNoteMoved(noteIndex_, previewStartBeat_, noteNumber_);
                }
                if (onNoteResized) {
                    onNoteResized(noteIndex_, previewLengthBeats_, true);
                }
                break;

            case DragMode::ResizeRight:
                if (onNoteResized) {
                    onNoteResized(noteIndex_, previewLengthBeats_, false);
                }
                break;

            default:
                break;
        }
    }

    // Deferred deselect: click on already-selected note without dragging
    if (deferredDeselect_ && !isDragging_) {
        if (onNoteSelected) {
            onNoteSelected(noteIndex_, false);
        }
    }
    deferredDeselect_ = false;

    // Clear copy drag ghost
    if (isCopyDrag_ && parentGrid_) {
        parentGrid_->setCopyDragPreview(0, 0, 0, {}, false, 0);
    }

    // Notify that drag has ended
    if (onNoteDragging) {
        onNoteDragging(noteIndex_, previewStartBeat_, false);
    }

    dragMode_ = DragMode::None;
    isDragging_ = false;
    isCopyDrag_ = false;
}

void NoteComponent::mouseMove(const juce::MouseEvent& e) {
    bool wasHoverLeft = hoverLeftEdge_;
    bool wasHoverRight = hoverRightEdge_;

    hoverLeftEdge_ = isOnLeftEdge(e.x);
    hoverRightEdge_ = isOnRightEdge(e.x);

    updateCursor();

    if (hoverLeftEdge_ != wasHoverLeft || hoverRightEdge_ != wasHoverRight) {
        repaint();
    }
}

void NoteComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    mouseIsOver_ = false;
    stopTimer();
    hoverLeftEdge_ = false;
    hoverRightEdge_ = false;
    updateCursor();
    repaint();
}

void NoteComponent::mouseDoubleClick(const juce::MouseEvent& /*e*/) {
    // Double-click to delete note
    if (onNoteDeleted) {
        onNoteDeleted(noteIndex_);
    }
}

void NoteComponent::mouseEnter(const juce::MouseEvent& /*e*/) {
    mouseIsOver_ = true;
    startTimer(50);  // Poll modifiers at 20Hz while mouse is over
    updateCursor();
}

void NoteComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        updateCursor();
        repaint();
    }
}

void NoteComponent::setGhost(bool ghost) {
    if (ghost_ != ghost) {
        ghost_ = ghost;
        repaint();
    }
}

void NoteComponent::updateFromNote(const MidiNote& note, juce::Colour colour) {
    noteNumber_ = note.noteNumber;
    startBeat_ = note.startBeat;
    lengthBeats_ = note.lengthBeats;
    velocity_ = note.velocity;
    colour_ = colour;
    repaint();
}

void NoteComponent::timerCallback() {
    if (mouseIsOver_) {
        updateCursor();
    } else {
        stopTimer();
    }
}

bool NoteComponent::isOnLeftEdge(int x) const {
    return x < RESIZE_HANDLE_WIDTH && isSelected_;
}

bool NoteComponent::isOnRightEdge(int x) const {
    return x > getWidth() - RESIZE_HANDLE_WIDTH && isSelected_;
}

void NoteComponent::updateCursor() {
    if (isSelected_ && (hoverLeftEdge_ || hoverRightEdge_)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else if (isSelected_ && juce::ModifierKeys::currentModifiers.isShiftDown()) {
        setMouseCursor(juce::MouseCursor::CopyingCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

}  // namespace magda
