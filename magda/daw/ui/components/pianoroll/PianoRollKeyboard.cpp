#include "PianoRollKeyboard.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "PitchFoldMap.hpp"
#include "core/ClipInfo.hpp"
#include "core/GestureRouter.hpp"

namespace magda {

PianoRollKeyboard::PianoRollKeyboard() {
    setOpaque(true);
}

int PianoRollKeyboard::rowCount() const {
    return foldMap_ ? foldMap_->rowCount() : (maxNote_ - minNote_ + 1);
}

int PianoRollKeyboard::noteForRow(int row) const {
    return foldMap_ ? foldMap_->noteForRow(row) : (maxNote_ - row);
}

void PianoRollKeyboard::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(juce::Colour(0xFF1a1a1a));
    g.fillRect(bounds);

    const int rows = rowCount();
    for (int row = 0; row < rows; row++) {
        const int note = noteForRow(row);
        int y = bounds.getY() + row * noteHeight_ - scrollOffsetY_;

        if (y + noteHeight_ < bounds.getY() || y > bounds.getBottom()) {
            continue;
        }

        auto keyArea = juce::Rectangle<int>(bounds.getX(), y, bounds.getWidth(), noteHeight_);
        keyArea = keyArea.getIntersection(bounds);
        if (keyArea.isEmpty()) {
            continue;
        }

        const bool isPressed = note >= 0 && note < static_cast<int>(pressedNotes_.size()) &&
                               pressedNotes_[static_cast<size_t>(note)];

        if (isPressed) {
            // Highlight color for pressed key
            g.setColour(juce::Colour(0xFF4A9EFF));  // Blue highlight
        } else if (isBlackKey(note)) {
            g.setColour(juce::Colour(0xFF1a1a1a));  // True black keys
        } else {
            g.setColour(juce::Colour(0xFFE8E8E8));  // True white keys
        }
        g.fillRect(keyArea);

        if (highlightedNotes_.find(note) != highlightedNotes_.end()) {
            g.setColour(juce::Colour(0x556688CC));
            g.fillRect(keyArea);
        }

        // Note names live in OctaveLabelStrip now (next to the keyboard).

        // Subtle separator line between white keys
        if (!isBlackKey(note)) {
            g.setColour(juce::Colour(0xFFCCCCCC));
            g.drawHorizontalLine(y + noteHeight_ - 1, static_cast<float>(bounds.getX()),
                                 static_cast<float>(bounds.getRight()));
        }
    }
}

void PianoRollKeyboard::setNoteHeight(int height) {
    if (noteHeight_ != height) {
        noteHeight_ = height;
        repaint();
    }
}

void PianoRollKeyboard::setNoteRange(int minNote, int maxNote) {
    minNote_ = minNote;
    maxNote_ = maxNote;
    repaint();
}

void PianoRollKeyboard::setScrollOffset(int offsetY) {
    if (scrollOffsetY_ != offsetY) {
        scrollOffsetY_ = offsetY;
        repaint();
    }
}

void PianoRollKeyboard::setNotePressed(int noteNumber, bool pressed) {
    if (noteNumber < 0 || noteNumber >= static_cast<int>(pressedNotes_.size()))
        return;

    auto& state = pressedNotes_[static_cast<size_t>(noteNumber)];
    if (state != pressed) {
        state = pressed;
        repaint();
    }
}

void PianoRollKeyboard::setHighlightedNotes(const std::set<int>& notes) {
    if (highlightedNotes_ != notes) {
        highlightedNotes_ = notes;
        repaint();
    }
}

void PianoRollKeyboard::clearPressedNotes() {
    bool changed = false;
    for (auto& pressed : pressedNotes_) {
        changed = changed || pressed;
        pressed = false;
    }

    currentPlayingNote_ = -1;
    isPlayingNote_ = false;

    if (changed)
        repaint();
}

bool PianoRollKeyboard::isBlackKey(int noteNumber) const {
    int note = noteNumber % 12;
    return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
}

juce::String PianoRollKeyboard::getNoteName(int noteNumber) const {
    static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
    int octave = (noteNumber / 12) - 2;  // C-2 convention (note 0 = C-2, note 60 = C3)
    int note = noteNumber % 12;
    return juce::String(noteNames[note]) + juce::String(octave);
}

int PianoRollKeyboard::yToNoteNumber(int y) const {
    int adjustedY = y + scrollOffsetY_;
    int row = juce::jlimit(0, rowCount() - 1, adjustedY / noteHeight_);
    return juce::jlimit(minNote_, maxNote_, noteForRow(row));
}

void PianoRollKeyboard::mouseDown(const juce::MouseEvent& event) {
    mouseDownX_ = event.x;
    mouseDownY_ = event.y;
    lastDragY_ = event.y;
    zoomStartHeight_ = noteHeight_;
    dragMode_ = DragMode::None;
    dragGestureAxis_ = GestureAxis::Horizontal;

    // Capture anchor note at mouse position
    zoomAnchorNote_ = yToNoteNumber(event.y);

    // Start note preview
    currentPlayingNote_ = yToNoteNumber(event.y);
    isPlayingNote_ = true;
    setNotePressed(currentPlayingNote_, true);

    DBG("Piano keyboard: Note pressed - " << currentPlayingNote_);

    if (onNotePreview) {
        DBG("Piano keyboard: Calling onNotePreview callback");
        onNotePreview(currentPlayingNote_, 100, true);  // Note on with velocity 100
    } else {
        DBG("Piano keyboard: WARNING - onNotePreview callback not set!");
    }

    repaint();  // Redraw to show highlight
}

void PianoRollKeyboard::mouseDrag(const juce::MouseEvent& event) {
    int deltaX = std::abs(event.x - mouseDownX_);
    int deltaY = std::abs(event.y - mouseDownY_);

    // Determine drag mode if not yet set
    if (dragMode_ == DragMode::None) {
        if (deltaX > DRAG_THRESHOLD || deltaY > DRAG_THRESHOLD) {
            // Stop note preview when drag starts
            if (isPlayingNote_ && onNotePreview) {
                DBG("Piano keyboard: Stopping note due to drag");
                onNotePreview(currentPlayingNote_, 0, false);  // Note off
                setNotePressed(currentPlayingNote_, false);
                isPlayingNote_ = false;
                currentPlayingNote_ = -1;
            }

            dragGestureAxis_ = (deltaX > deltaY) ? GestureAxis::Horizontal : GestureAxis::Vertical;
            const int dragDelta = dragGestureAxis_ == GestureAxis::Horizontal
                                      ? event.x - mouseDownX_
                                      : mouseDownY_ - event.y;
            const auto gesture = GestureRouter::getInstance().resolveDrag(
                GestureContext::PianoRoll, GestureArea::Keyboard, dragGestureAxis_, event.mods,
                static_cast<float>(dragDelta), {mouseDownX_, mouseDownY_});
            dragMode_ = gesture.type == GestureActionType::ZoomVertical
                            ? DragMode::Zooming
                            : (dragGestureAxis_ == GestureAxis::Vertical ? DragMode::Scrolling
                                                                         : DragMode::None);
        }
    }

    if (dragMode_ == DragMode::Zooming) {
        const int dragDelta = dragGestureAxis_ == GestureAxis::Horizontal ? event.x - mouseDownX_
                                                                          : mouseDownY_ - event.y;
        const auto gesture = GestureRouter::getInstance().resolveDrag(
            GestureContext::PianoRoll, GestureArea::Keyboard, dragGestureAxis_, event.mods,
            static_cast<float>(dragDelta), {mouseDownX_, mouseDownY_});
        if (gesture.type != GestureActionType::ZoomVertical)
            return;

        int newHeight = static_cast<int>(
            std::round(static_cast<double>(zoomStartHeight_) * std::pow(2.0, gesture.magnitude)));

        // Clamp to reasonable limits
        newHeight = juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT,
                                 ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT, newHeight);

        if (onZoomChanged && newHeight != noteHeight_) {
            onZoomChanged(newHeight, zoomAnchorNote_, mouseDownY_);
        }
    } else if (dragMode_ == DragMode::Scrolling) {
        // Calculate scroll delta (drag up scrolls up, drag down scrolls down)
        int scrollDelta = lastDragY_ - event.y;
        lastDragY_ = event.y;

        if (onScrollRequested && scrollDelta != 0) {
            onScrollRequested(scrollDelta);
        }
    }
}

void PianoRollKeyboard::mouseUp(const juce::MouseEvent& /*event*/) {
    // Stop note preview if still playing
    if (isPlayingNote_ && onNotePreview) {
        DBG("Piano keyboard: Note released - " << currentPlayingNote_);
        onNotePreview(currentPlayingNote_, 0, false);  // Note off
        setNotePressed(currentPlayingNote_, false);
        isPlayingNote_ = false;
        currentPlayingNote_ = -1;
    }

    dragMode_ = DragMode::None;
}

void PianoRollKeyboard::mouseWheelMove(const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel) {
    // Alt+wheel = note-height (vertical) zoom, resolved via GestureRouter so the
    // binding matches the piano-roll grid and is configurable (#1350). The
    // zoom magnitude stays in this handler.
    const auto gesture = GestureRouter::getInstance().resolve(GestureContext::PianoRoll, wheel,
                                                              event.mods, event.getPosition());
    if (gesture.type == GestureActionType::ZoomVertical && onZoomChanged) {
        const int anchorNote = yToNoteNumber(event.y);
        const int heightDelta = wheel.deltaY > 0 ? 2 : -2;
        onZoomChanged(juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT,
                                   ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT, noteHeight_ + heightDelta),
                      anchorNote, event.y);
        return;
    }

    // Scroll vertically when wheel is used over the keyboard
    if (onScrollRequested) {
        // Convert wheel delta to pixels
        int scrollAmount = static_cast<int>(-wheel.deltaY * 100.0f);
        if (scrollAmount != 0) {
            onScrollRequested(scrollAmount);
        }
    }
}

}  // namespace magda
