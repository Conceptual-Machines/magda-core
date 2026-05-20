#include "OctaveLabelStrip.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

OctaveLabelStrip::OctaveLabelStrip() {
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
}

void OctaveLabelStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    g.setColour(juce::Colour(0xFF1a1a1a));
    g.fillRect(bounds);

    g.setColour(juce::Colour(0xFFB3B3B3));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));

    static const char* kNoteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                       "F#", "G",  "G#", "A",  "A#", "B"};

    // Draw the label centred on the C row so it aligns with the keyboard's
    // own per-note C label at higher zoom levels. A hairline separator
    // sits at the octave boundary (the bottom of the C row), giving each
    // label a "header for the octave above" feel.
    const int labelHeight = LABEL_HEIGHT;
    for (int note = minNote_; note <= maxNote_; note += 12) {
        if (note % 12 != 0)
            continue;
        const int y = bounds.getY() + (maxNote_ - note) * noteHeight_ - scrollOffsetY_;
        const int labelY = y + (noteHeight_ - labelHeight) / 2;
        if (labelY + labelHeight < bounds.getY() || labelY > bounds.getBottom())
            continue;

        const int octave = (note / 12) - 2;
        auto labelArea =
            juce::Rectangle<int>(bounds.getX(), labelY, bounds.getWidth(), labelHeight);
        g.setColour(juce::Colour(0xFFB3B3B3));
        g.drawText(juce::String(kNoteNames[note % 12]) + juce::String(octave),
                   labelArea.reduced(2, 0), juce::Justification::centred, false);

        // Hairline at the octave boundary (bottom of C row).
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(y + noteHeight_, static_cast<float>(bounds.getX()),
                             static_cast<float>(bounds.getRight()));
    }

    // Right border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(bounds.getRight() - 1, static_cast<float>(bounds.getY()),
                       static_cast<float>(bounds.getBottom()));
}

void OctaveLabelStrip::setNoteHeight(int height) {
    if (noteHeight_ != height) {
        noteHeight_ = height;
        repaint();
    }
}

void OctaveLabelStrip::setNoteRange(int minNote, int maxNote) {
    minNote_ = minNote;
    maxNote_ = maxNote;
    repaint();
}

void OctaveLabelStrip::setScrollOffset(int offsetY) {
    if (scrollOffsetY_ != offsetY) {
        scrollOffsetY_ = offsetY;
        repaint();
    }
}

}  // namespace magda
