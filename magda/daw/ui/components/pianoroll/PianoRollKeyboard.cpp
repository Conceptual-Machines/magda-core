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

}  // namespace magda
