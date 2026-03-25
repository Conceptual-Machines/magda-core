#include "ChordBlockComponent.hpp"

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

        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (!container)
            return;

        // Build drag description with chord data
        auto* dragInfo = new juce::DynamicObject();
        dragInfo->setProperty("type", "chordBlock");
        dragInfo->setProperty("chordName", chord_.getDisplayName());

        // Encode notes as an array of [noteNumber, velocity] pairs
        auto notesArray = juce::Array<juce::var>();
        for (const auto& note : chord_.notes) {
            auto pair = juce::Array<juce::var>();
            pair.add(note.noteNumber);
            pair.add(note.velocity);
            notesArray.add(juce::var(pair));
        }
        dragInfo->setProperty("notes", notesArray);

        // Create a snapshot for the drag image
        auto snapshot = createComponentSnapshot(getLocalBounds(), true, 1.0f);
        container->startDragging(juce::var(dragInfo), this, juce::ScaledImage(snapshot), true);
    }
}

}  // namespace magda::daw::ui
