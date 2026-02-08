#include "NoteInspector.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

NoteInspector::NoteInspector() {
    // ========================================================================
    // Note properties section
    // ========================================================================

    // Note count (shown when multiple notes selected)
    noteCountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noteCountLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(noteCountLabel_);

    // Note pitch
    notePitchLabel_.setText("Pitch", juce::dontSendNotification);
    notePitchLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    notePitchLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(notePitchLabel_);

    notePitchValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::MidiNote);
    notePitchValue_->setRange(0.0, 127.0, 60.0);  // MIDI note range
    notePitchValue_->onValueChange = [this]() {
        if (noteSelection_.isValid() && noteSelection_.isSingleNote()) {
            const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
            if (clip && noteSelection_.noteIndices[0] < clip->midiNotes.size()) {
                const auto& note = clip->midiNotes[noteSelection_.noteIndices[0]];
                int newPitch = static_cast<int>(notePitchValue_->getValue());
                auto cmd = std::make_unique<magda::MoveMidiNoteCommand>(
                    noteSelection_.clipId, noteSelection_.noteIndices[0], note.startBeat, newPitch);
                magda::UndoManager::getInstance().executeCommand(std::move(cmd));
            }
        }
    };
    addChildComponent(*notePitchValue_);

    // Note velocity
    noteVelocityLabel_.setText("Velocity", juce::dontSendNotification);
    noteVelocityLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    noteVelocityLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(noteVelocityLabel_);

    noteVelocityValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Integer);
    noteVelocityValue_->setRange(1.0, 127.0, 100.0);
    noteVelocityValue_->onValueChange = [this]() {
        if (noteSelection_.isValid() && noteSelection_.isSingleNote()) {
            int newVelocity = static_cast<int>(noteVelocityValue_->getValue());
            auto cmd = std::make_unique<magda::SetMidiNoteVelocityCommand>(
                noteSelection_.clipId, noteSelection_.noteIndices[0], newVelocity);
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };
    addChildComponent(*noteVelocityValue_);

    // Note start
    noteStartLabel_.setText("Start", juce::dontSendNotification);
    noteStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    noteStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(noteStartLabel_);

    noteStartValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noteStartValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addChildComponent(noteStartValue_);

    // Note length
    noteLengthLabel_.setText("Length", juce::dontSendNotification);
    noteLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    noteLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addChildComponent(noteLengthLabel_);

    noteLengthValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Beats);
    noteLengthValue_->setRange(0.0625, 16.0, 1.0);  // 1/16 note to 16 beats
    noteLengthValue_->onValueChange = [this]() {
        if (noteSelection_.isValid() && noteSelection_.isSingleNote()) {
            double newLength = noteLengthValue_->getValue();
            auto cmd = std::make_unique<magda::ResizeMidiNoteCommand>(
                noteSelection_.clipId, noteSelection_.noteIndices[0], newLength);
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }
    };
    addChildComponent(*noteLengthValue_);
}

NoteInspector::~NoteInspector() = default;

void NoteInspector::onActivated() {
    // No listeners needed - updates come from parent InspectorContainer
}

void NoteInspector::onDeactivated() {
    // No cleanup needed
}

void NoteInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
}

void NoteInspector::resized() {
    auto bounds = getLocalBounds().reduced(10);

    if (!noteSelection_.isValid()) {
        return;
    }

    if (noteSelection_.isSingleNote()) {
        // Single note: show all properties
        notePitchLabel_.setBounds(bounds.removeFromTop(16));
        notePitchValue_->setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(8);

        noteVelocityLabel_.setBounds(bounds.removeFromTop(16));
        noteVelocityValue_->setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(8);

        noteStartLabel_.setBounds(bounds.removeFromTop(16));
        noteStartValue_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(8);

        noteLengthLabel_.setBounds(bounds.removeFromTop(16));
        noteLengthValue_->setBounds(bounds.removeFromTop(24));
    } else {
        // Multiple notes: show count only
        noteCountLabel_.setBounds(bounds.removeFromTop(24));
    }
}

void NoteInspector::setSelectedNotes(const magda::NoteSelection& selection) {
    noteSelection_ = selection;
    updateFromSelectedNotes();
}

void NoteInspector::updateFromSelectedNotes() {
    bool hasSelection = noteSelection_.isValid();
    bool isSingle = noteSelection_.isSingleNote();

    showNoteControls(hasSelection);

    if (!hasSelection) {
        return;
    }

    if (isSingle) {
        // Single note: populate all fields
        const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
        if (clip && noteSelection_.noteIndices[0] < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[noteSelection_.noteIndices[0]];

            notePitchValue_->setValue(note.noteNumber, juce::dontSendNotification);
            noteVelocityValue_->setValue(note.velocity, juce::dontSendNotification);

            // Format start position (e.g., "1.2.000")
            noteStartValue_.setText(juce::String(note.startBeat, 3), juce::dontSendNotification);

            noteLengthValue_->setValue(note.lengthBeats, juce::dontSendNotification);
        }
    } else {
        // Multiple notes: show count
        noteCountLabel_.setText(juce::String(noteSelection_.noteIndices.size()) + " notes selected",
                                juce::dontSendNotification);
    }

    resized();
}

void NoteInspector::showNoteControls(bool show) {
    bool isSingle = noteSelection_.isSingleNote();

    // Single note controls
    notePitchLabel_.setVisible(show && isSingle);
    notePitchValue_->setVisible(show && isSingle);
    noteVelocityLabel_.setVisible(show && isSingle);
    noteVelocityValue_->setVisible(show && isSingle);
    noteStartLabel_.setVisible(show && isSingle);
    noteStartValue_.setVisible(show && isSingle);
    noteLengthLabel_.setVisible(show && isSingle);
    noteLengthValue_->setVisible(show && isSingle);

    // Multiple notes control
    noteCountLabel_.setVisible(show && !isSingle);
}

}  // namespace magda::daw::ui
