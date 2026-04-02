#include "ChordBlockComponent.hpp"

#include "core/MidiFileWriter.hpp"
#include "project/ProjectManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ChordBlockComponent::ChordBlockComponent(const magda::music::Chord& chord) : chord_(chord) {
    setName("ChordBlock");
}

void ChordBlockComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Background — accent blue at 20% alpha (matches piano roll chord row style)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border on hover
    if (isMouseOver()) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    }

    auto textBounds = getLocalBounds().reduced(4, 0);

    // Chord name
    auto displayName = chord_.getDisplayName();
    g.setColour(DarkTheme::getTextColour());
    if (degree_.isEmpty()) {
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText(displayName, textBounds, juce::Justification::centred);
    } else {
        // Two-line layout: chord name on top, degree below
        auto nameArea = textBounds.removeFromTop(textBounds.getHeight() / 2);
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText(displayName, nameArea, juce::Justification::centredBottom);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.drawText(degree_, textBounds, juce::Justification::centredTop);
    }
}

void ChordBlockComponent::mouseDown(const juce::MouseEvent& /*e*/) {
    dragging_ = false;
    if (onClicked)
        onClicked(chord_);
}

void ChordBlockComponent::mouseUp(const juce::MouseEvent& /*e*/) {
    if (!dragging_ && onReleased)
        onReleased();
}

void ChordBlockComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_ && e.getDistanceFromDragStart() > 4) {
        dragging_ = true;
        // Stop preview when drag starts
        if (onReleased)
            onReleased();

        // Convert chord notes to MidiNotes (1 bar = 4 beats)
        constexpr double chordLength = 4.0;
        std::vector<magda::MidiNote> midiNotes;
        for (const auto& cn : chord_.notes) {
            magda::MidiNote note;
            note.noteNumber = std::clamp(cn.noteNumber, 0, 127);
            note.velocity = std::clamp(cn.velocity, 1, 127);
            note.startBeat = 0.0;
            note.lengthBeats = chordLength;
            midiNotes.push_back(note);
        }

        if (midiNotes.empty())
            return;

        double tempo = magda::ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        if (tempo <= 0.0)
            tempo = 120.0;

        std::vector<daw::ChordMarker> markers;
        markers.push_back({0.0, chordLength, chord_.getDisplayName()});

        auto tempFile = daw::MidiFileWriter::writeToTempFile(midiNotes, tempo,
                                                             chord_.getDisplayName(), markers);
        if (tempFile.existsAsFile()) {
            setAlpha(0.4f);
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                juce::StringArray{tempFile.getFullPathName()}, false, this);
            setAlpha(1.0f);
        }
    }
}

}  // namespace magda::daw::ui
