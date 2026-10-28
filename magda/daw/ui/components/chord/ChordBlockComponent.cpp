// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChordBlockComponent.hpp"

#include <utility>

#include "core/MidiFileWriter.hpp"
#include "music/NotationSettings.hpp"
#include "project/ProjectManager.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ChordBlockComponent::ChordBlockComponent(magda::music::Chord chord) : chord_(std::move(chord)) {
    setName("ChordBlock");
    setRepaintsOnMouseActivity(true);
}

void ChordBlockComponent::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto fill = highlighted_    ? ActiveTheme::DEVICE_ROW_SELECTED
                      : isMouseOver() ? ActiveTheme::DEVICE_ROW_HOVER
                                      : ActiveTheme::DEVICE_FIELD;
    g.setColour(ActiveTheme::getColour(fill));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(ActiveTheme::getColour(highlighted_ ? ActiveTheme::DEVICE_ROW_SELECTED_BORDER
                                                    : ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    // Tall tiles (the suggestion grid) read larger than the history row's.
    const bool tall = getHeight() >= 44;
    auto text = getLocalBounds().reduced(tall ? 12 : 9, tall ? 9 : 4);

    if (function_ != chord_function::HarmonicFunction::None) {
        const auto dot = juce::Rectangle<float>(6.0f, 6.0f)
                             .withCentre({static_cast<float>(getWidth() - (tall ? 15 : 10)),
                                          tall ? 15.0f : 10.0f});
        g.setColour(chord_function::functionColour(function_));
        g.fillEllipse(dot);
        text.removeFromRight(10);
    }

    const auto name = magda::music::NotationSettings::getInstance().format(chord_.getDisplayName());
    const float nameSize = tall ? 15.0f : 12.5f;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.setFont(fonts.getUIFontMedium(nameSize));
    if (degree_.isEmpty()) {
        g.drawText(name, text, juce::Justification::centredLeft, true);
        return;
    }
    const int nameHeight = juce::roundToInt(nameSize * 1.35f);
    const int degreeHeight = tall ? 14 : 11;
    auto stack = text.withSizeKeepingCentre(text.getWidth(), nameHeight + degreeHeight);
    g.drawText(name, stack.removeFromTop(nameHeight), juce::Justification::centredLeft, true);
    g.setColour(
        ActiveTheme::getColour(highlighted_ ? ActiveTheme::DEVICE_BLUE : ActiveTheme::DEVICE_DIM));
    g.setFont(fonts.getMonoFont(tall ? 10.5f : 9.0f));
    g.drawText(degree_, stack, juce::Justification::centredLeft, true);
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
            magda::dnd::startFilesDrag(this, juce::StringArray{tempFile.getFullPathName()});
            setAlpha(1.0f);
        }
    }
}

}  // namespace magda::daw::ui
