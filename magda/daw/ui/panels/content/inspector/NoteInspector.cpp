#include "NoteInspector.hpp"

#include <algorithm>

#include "../../state/TimelineController.hpp"
#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "clip/ClipInspectorStyle.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

NoteInspector::NoteInspector() {
    headerName_.setFont(FontManager::getInstance().getUIFont(14.0f));
    headerName_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    headerName_.setBorderSize({});
    addChildComponent(headerName_);
    headerSubline_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    headerSubline_.setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    headerSubline_.setBorderSize({});
    addChildComponent(headerSubline_);

    clip_style::caption(notePitchLabel_, "Pitch");
    clip_style::caption(noteVelocityLabel_, "Velocity");
    clip_style::caption(noteStartLabel_, "Start");
    clip_style::caption(noteLengthLabel_, "Length");
    for (auto* label : {&notePitchLabel_, &noteVelocityLabel_, &noteStartLabel_, &noteLengthLabel_})
        addChildComponent(*label);

    notePitchValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::MidiNote);
    notePitchValue_->setRange(0.0, 127.0, 60.0);
    clip_style::field(*notePitchValue_);
    notePitchValue_->onValueChange = [this]() {
        if (!noteSelection_.isValid())
            return;

        double currentValue = notePitchValue_->getValue();
        int delta = static_cast<int>(std::round(currentValue - multiPitchDragStart_));
        if (delta == 0)
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
        if (!clip)
            return;

        std::vector<magda::MoveMultipleMidiNotesCommand::NoteMove> moves;
        for (size_t idx : noteSelection_.noteIndices) {
            if (idx >= clip->midiNotes.size())
                continue;
            const auto& note = clip->midiNotes[idx];
            int newPitch = juce::jlimit(0, 127, note.noteNumber + delta);
            moves.push_back({idx, note.startBeat, newPitch});
        }

        if (!moves.empty()) {
            auto cmd = std::make_unique<magda::MoveMultipleMidiNotesCommand>(noteSelection_.clipId,
                                                                             std::move(moves));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }

        multiPitchDragStart_ = currentValue;
        computeMultiRange();
        refreshMultiRangeDisplay();
    };
    addChildComponent(*notePitchValue_);

    noteVelocityValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Integer);
    noteVelocityValue_->setRange(1.0, 127.0, 100.0);
    clip_style::field(*noteVelocityValue_);
    noteVelocityValue_->onValueChange = [this]() {
        if (!noteSelection_.isValid())
            return;

        double currentValue = noteVelocityValue_->getValue();
        int delta = static_cast<int>(std::round(currentValue - multiVelocityDragStart_));
        if (delta == 0)
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
        if (!clip)
            return;

        std::vector<std::pair<size_t, int>> velocities;
        for (size_t idx : noteSelection_.noteIndices) {
            if (idx >= clip->midiNotes.size())
                continue;
            int newVel = juce::jlimit(1, 127, clip->midiNotes[idx].velocity + delta);
            velocities.emplace_back(idx, newVel);
        }

        if (!velocities.empty()) {
            auto cmd = std::make_unique<magda::SetMultipleNoteVelocitiesCommand>(
                noteSelection_.clipId, std::move(velocities));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }

        multiVelocityDragStart_ = currentValue;
        computeMultiRange();
        refreshMultiRangeDisplay();
    };
    addChildComponent(*noteVelocityValue_);

    noteStartValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::BarsBeats);
    noteStartValue_->setRange(0.0, 256.0, 0.0);
    noteStartValue_->setBarsBeatsIsPosition(true);
    clip_style::field(*noteStartValue_);
    noteStartValue_->onValueChange = [this]() {
        if (!noteSelection_.isValid())
            return;

        double currentValue = noteStartValue_->getValue();
        double delta = currentValue - multiStartDragStart_;
        if (std::abs(delta) < 0.001)
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
        if (!clip)
            return;

        std::vector<magda::MoveMultipleMidiNotesCommand::NoteMove> moves;
        for (size_t idx : noteSelection_.noteIndices) {
            if (idx >= clip->midiNotes.size())
                continue;
            const auto& note = clip->midiNotes[idx];
            double newStart = juce::jmax(0.0, note.startBeat + delta);
            moves.push_back({idx, newStart, note.noteNumber});
        }

        if (!moves.empty()) {
            auto cmd = std::make_unique<magda::MoveMultipleMidiNotesCommand>(noteSelection_.clipId,
                                                                             std::move(moves));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }

        multiStartDragStart_ = currentValue;
        computeMultiRange();
        refreshMultiRangeDisplay();
    };
    addChildComponent(*noteStartValue_);

    noteLengthValue_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::BarsBeats);
    noteLengthValue_->setRange(0.0625, 16.0, 1.0);
    noteLengthValue_->setBarsBeatsIsPosition(false);
    clip_style::field(*noteLengthValue_);
    noteLengthValue_->onValueChange = [this]() {
        if (!noteSelection_.isValid())
            return;

        double currentValue = noteLengthValue_->getValue();
        double delta = currentValue - multiLengthDragStart_;
        constexpr double MIN_LENGTH = 1.0 / 16.0;
        if (std::abs(delta) < 0.001)
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
        if (!clip)
            return;

        std::vector<std::pair<size_t, double>> lengths;
        for (size_t idx : noteSelection_.noteIndices) {
            if (idx >= clip->midiNotes.size())
                continue;
            double newLen = juce::jmax(MIN_LENGTH, clip->midiNotes[idx].lengthBeats + delta);
            lengths.emplace_back(idx, newLen);
        }

        if (!lengths.empty()) {
            auto cmd = std::make_unique<magda::ResizeMultipleMidiNotesCommand>(
                noteSelection_.clipId, std::move(lengths));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        }

        multiLengthDragStart_ = currentValue;
        computeMultiRange();
        refreshMultiRangeDisplay();
    };
    addChildComponent(*noteLengthValue_);
}

NoteInspector::~NoteInspector() {
    magda::ClipManager::getInstance().removeListener(this);
}

void NoteInspector::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
}

void NoteInspector::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void NoteInspector::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getBackgroundColour());
}

void NoteInspector::resized() {
    auto bounds = getLocalBounds().reduced(10, 8);
    if (!noteSelection_.isValid())
        return;

    constexpr int kHeaderHeight = 40;
    constexpr int kRowLabelWidth = 62;
    auto header = bounds.removeFromTop(kHeaderHeight).reduced(0, 2);
    headerName_.setBounds(header.removeFromTop(20));
    headerSubline_.setBounds(header.removeFromTop(14));
    bounds.removeFromTop(6);

    auto row = [&](juce::Label& caption, juce::Component& value) {
        auto r = bounds.removeFromTop(clip_style::kFieldHeight);
        caption.setBounds(r.removeFromLeft(kRowLabelWidth));
        r.removeFromLeft(4);
        value.setBounds(r);
        bounds.removeFromTop(clip_style::kGap);
    };
    row(notePitchLabel_, *notePitchValue_);
    row(noteVelocityLabel_, *noteVelocityValue_);
    row(noteStartLabel_, *noteStartValue_);
    row(noteLengthLabel_, *noteLengthValue_);
}

void NoteInspector::setSelectedNotes(const magda::NoteSelection& selection) {
    noteSelection_ = selection;
    updateFromSelectedNotes();
}

void NoteInspector::updateFromSelectedNotes() {
    bool hasSelection = noteSelection_.isValid();

    showNoteControls(hasSelection);

    if (!hasSelection) {
        return;
    }

    // Show note count for multi-selection
    computeMultiRange();
    if (!multiRange_.valid)
        return;

    const auto count = noteSelection_.getCount();
    headerName_.setText(count > 1 ? juce::String(static_cast<int>(count)) + " notes"
                                  : midiNoteToName(multiRange_.minPitch),
                        juce::dontSendNotification);
    const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
    headerSubline_.setText(clip != nullptr ? clip->name : juce::String(),
                           juce::dontSendNotification);

    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& tempo = controller->getState().tempo;
        noteStartValue_->setTimeSignature(tempo.timeSignatureNumerator,
                                          tempo.timeSignatureDenominator);
        noteLengthValue_->setTimeSignature(tempo.timeSignatureNumerator,
                                           tempo.timeSignatureDenominator);
    }

    // Set virtual values centered at midpoints for drag starting point
    double midPitch = (multiRange_.minPitch + multiRange_.maxPitch) / 2.0;
    double midVelocity = (multiRange_.minVelocity + multiRange_.maxVelocity) / 2.0;
    double midStart = (multiRange_.minStart + multiRange_.maxStart) / 2.0;
    double midLength = (multiRange_.minLength + multiRange_.maxLength) / 2.0;

    notePitchValue_->setRange(0.0, 127.0, midPitch);
    notePitchValue_->setValue(midPitch, juce::dontSendNotification);
    multiPitchDragStart_ = midPitch;

    noteVelocityValue_->setRange(1.0, 127.0, midVelocity);
    noteVelocityValue_->setValue(midVelocity, juce::dontSendNotification);
    multiVelocityDragStart_ = midVelocity;

    noteStartValue_->setRange(0.0, 256.0, midStart);
    noteStartValue_->setValue(midStart, juce::dontSendNotification);
    multiStartDragStart_ = midStart;

    noteLengthValue_->setRange(0.0625, 32.0, midLength);
    noteLengthValue_->setValue(midLength, juce::dontSendNotification);
    multiLengthDragStart_ = midLength;

    notePitchValue_->setDoubleClickResetsValue(true);
    noteVelocityValue_->setDoubleClickResetsValue(true);
    noteStartValue_->setDoubleClickResetsValue(true);
    noteLengthValue_->setDoubleClickResetsValue(true);

    refreshMultiRangeDisplay();

    resized();
}

void NoteInspector::computeMultiRange() {
    multiRange_ = NoteRange{};

    const auto* clip = magda::ClipManager::getInstance().getClip(noteSelection_.clipId);
    if (!clip)
        return;

    bool first = true;
    for (size_t idx : noteSelection_.noteIndices) {
        if (idx >= clip->midiNotes.size())
            continue;
        const auto& note = clip->midiNotes[idx];

        if (first) {
            multiRange_.valid = true;
            multiRange_.minPitch = multiRange_.maxPitch = note.noteNumber;
            multiRange_.minVelocity = multiRange_.maxVelocity = note.velocity;
            multiRange_.minLength = multiRange_.maxLength = note.lengthBeats;
            multiRange_.minStart = multiRange_.maxStart = note.startBeat;
            first = false;
        } else {
            multiRange_.minPitch = juce::jmin(multiRange_.minPitch, note.noteNumber);
            multiRange_.maxPitch = juce::jmax(multiRange_.maxPitch, note.noteNumber);
            multiRange_.minVelocity = juce::jmin(multiRange_.minVelocity, note.velocity);
            multiRange_.maxVelocity = juce::jmax(multiRange_.maxVelocity, note.velocity);
            multiRange_.minLength = juce::jmin(multiRange_.minLength, note.lengthBeats);
            multiRange_.maxLength = juce::jmax(multiRange_.maxLength, note.lengthBeats);
            multiRange_.minStart = juce::jmin(multiRange_.minStart, note.startBeat);
            multiRange_.maxStart = juce::jmax(multiRange_.maxStart, note.startBeat);
        }
    }
}

void NoteInspector::refreshMultiRangeDisplay() {
    if (!multiRange_.valid)
        return;

    // Pitch range
    if (multiRange_.minPitch == multiRange_.maxPitch) {
        notePitchValue_->setTextOverride(midiNoteToName(multiRange_.minPitch));
    } else {
        notePitchValue_->setTextOverride(midiNoteToName(multiRange_.minPitch) + " \xe2\x80\x93 " +
                                         midiNoteToName(multiRange_.maxPitch));
    }

    // Velocity range
    if (multiRange_.minVelocity == multiRange_.maxVelocity) {
        noteVelocityValue_->setTextOverride(juce::String(multiRange_.minVelocity));
    } else {
        noteVelocityValue_->setTextOverride(juce::String(multiRange_.minVelocity) +
                                            " \xe2\x80\x93 " +
                                            juce::String(multiRange_.maxVelocity));
    }

    // Start range (position format: 1-indexed)
    int numerator = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    int denominator = magda::DEFAULT_TIME_SIGNATURE_DENOMINATOR;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        numerator = controller->getState().tempo.timeSignatureNumerator;
        denominator = controller->getState().tempo.timeSignatureDenominator;
    }
    auto formatBarsBeats = [numerator, denominator](double val, bool isPosition) -> juce::String {
        const auto position = magda::toBarsBeatsTicks(val, numerator, denominator);
        int offset = isPosition ? 1 : 0;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%d.%d.%03d", position.bars + offset,
                      position.beats + offset, position.ticks);
        return {buffer};
    };

    if (std::abs(multiRange_.minStart - multiRange_.maxStart) < 0.001) {
        noteStartValue_->setTextOverride(formatBarsBeats(multiRange_.minStart, true));
    } else {
        noteStartValue_->setTextOverride(formatBarsBeats(multiRange_.minStart, true) +
                                         " \xe2\x80\x93 " +
                                         formatBarsBeats(multiRange_.maxStart, true));
    }

    // Length range (duration format: 0-indexed)
    if (std::abs(multiRange_.minLength - multiRange_.maxLength) < 0.001) {
        noteLengthValue_->setTextOverride(formatBarsBeats(multiRange_.minLength, false));
    } else {
        noteLengthValue_->setTextOverride(formatBarsBeats(multiRange_.minLength, false) +
                                          " \xe2\x80\x93 " +
                                          formatBarsBeats(multiRange_.maxLength, false));
    }
}

void NoteInspector::showNoteControls(bool show) {
    bool hasNotes = noteSelection_.isValid();

    notePitchLabel_.setVisible(show && hasNotes);
    notePitchValue_->setVisible(show && hasNotes);
    noteVelocityLabel_.setVisible(show && hasNotes);
    noteVelocityValue_->setVisible(show && hasNotes);
    noteStartLabel_.setVisible(show && hasNotes);
    noteStartValue_->setVisible(show && hasNotes);
    noteLengthLabel_.setVisible(show && hasNotes);
    noteLengthValue_->setVisible(show && hasNotes);

    headerName_.setVisible(show && hasNotes);
    headerSubline_.setVisible(show && hasNotes);
}

juce::String NoteInspector::midiNoteToName(int noteNumber) {
    static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
    noteNumber = juce::jlimit(0, 127, noteNumber);
    int octave = (noteNumber / 12) - 2;
    int noteIndex = noteNumber % 12;
    return juce::String(noteNames[noteIndex]) + juce::String(octave);
}

void NoteInspector::clipPropertyChanged(magda::ClipId clipId) {
    if (!noteSelection_.isValid() || noteSelection_.clipId != clipId)
        return;

    refreshDisplayValues();
}

void NoteInspector::refreshDisplayValues() {
    if (!noteSelection_.isValid())
        return;

    // Refresh range display without resetting drag state
    computeMultiRange();
    refreshMultiRangeDisplay();
}

}  // namespace magda::daw::ui
