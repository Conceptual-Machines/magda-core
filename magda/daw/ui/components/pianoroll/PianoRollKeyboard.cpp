#include "PianoRollKeyboard.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

PianoRollKeyboard::PianoRollKeyboard() {
    setOpaque(true);
}

void PianoRollKeyboard::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(juce::Colour(0xFF1a1a1a));
    g.fillRect(bounds);

    for (int note = minNote_; note <= maxNote_; note++) {
        int y = bounds.getY() + (maxNote_ - note) * noteHeight_ - scrollOffsetY_;

        if (y + noteHeight_ < bounds.getY() || y > bounds.getBottom()) {
            continue;
        }

        auto keyArea = juce::Rectangle<int>(bounds.getX(), y, bounds.getWidth(), noteHeight_);
        keyArea = keyArea.getIntersection(bounds);
        if (keyArea.isEmpty()) {
            continue;
        }

        if (isBlackKey(note)) {
            g.setColour(juce::Colour(0xFF2a2a2a));
        } else {
            g.setColour(juce::Colour(0xFF4a4a4a));
        }
        g.fillRect(keyArea);

        // Draw note name for C notes
        if (note % 12 == 0) {
            g.setColour(juce::Colours::white);
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText(getNoteName(note), keyArea.reduced(4, 0), juce::Justification::centredLeft,
                       false);
        }

        // Subtle separator line
        if (!isBlackKey(note)) {
            g.setColour(juce::Colour(0xFF3a3a3a));
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

bool PianoRollKeyboard::isBlackKey(int noteNumber) const {
    int note = noteNumber % 12;
    return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
}

juce::String PianoRollKeyboard::getNoteName(int noteNumber) const {
    static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
    int octave = (noteNumber / 12) - 1;
    int note = noteNumber % 12;
    return juce::String(noteNames[note]) + juce::String(octave);
}

int PianoRollKeyboard::yToNoteNumber(int y) const {
    int adjustedY = y + scrollOffsetY_;
    int note = maxNote_ - (adjustedY / noteHeight_);
    return juce::jlimit(minNote_, maxNote_, note);
}

void PianoRollKeyboard::mouseDown(const juce::MouseEvent& event) {
    mouseDownX_ = event.x;
    mouseDownY_ = event.y;
    lastDragY_ = event.y;
    zoomStartHeight_ = noteHeight_;
    dragMode_ = DragMode::None;

    // Capture anchor note at mouse position
    zoomAnchorNote_ = yToNoteNumber(event.y);
}

void PianoRollKeyboard::mouseDrag(const juce::MouseEvent& event) {
    int deltaX = std::abs(event.x - mouseDownX_);
    int deltaY = std::abs(event.y - mouseDownY_);

    // Determine drag mode if not yet set
    if (dragMode_ == DragMode::None) {
        if (deltaX > DRAG_THRESHOLD || deltaY > DRAG_THRESHOLD) {
            // Vertical drag = scroll (along keyboard), horizontal drag = zoom
            dragMode_ = (deltaY > deltaX) ? DragMode::Scrolling : DragMode::Zooming;
        }
    }

    if (dragMode_ == DragMode::Zooming) {
        // Drag left = zoom out (smaller notes), drag right = zoom in (larger notes)
        int xDelta = event.x - mouseDownX_;

        // Linear zoom - each 10 pixels of drag changes height by 1
        int heightDelta = xDelta / 10;
        int newHeight = zoomStartHeight_ + heightDelta;

        // Clamp to reasonable limits
        newHeight = juce::jlimit(6, 40, newHeight);

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
    dragMode_ = DragMode::None;
}

void PianoRollKeyboard::mouseWheelMove(const juce::MouseEvent& /*event*/,
                                       const juce::MouseWheelDetails& wheel) {
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
