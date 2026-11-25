// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PianoRollKeyboard.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "MidiEditorKey.hpp"
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

    // Keys stay physical (light naturals, dark accidentals) in every theme. With a lit key,
    // out-of-scale naturals grey out and the root is labelled in orange.
    const auto naturalKey = ActiveTheme::getColour(ActiveTheme::MIDI_KEY_WHITE);
    const auto naturalOutOfScale = ActiveTheme::getColour(ActiveTheme::MIDI_KEY_WHITE_OUT);
    const auto accidentalKey = ActiveTheme::getColour(ActiveTheme::MIDI_KEY_BLACK);
    const auto keySeparator = naturalKey.darker(0.2f);
    const auto key = MidiEditorKeyState::getInstance().activeScale();

    // Background
    g.setColour(accidentalKey);
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
            g.setColour(ActiveTheme::getColour(ActiveTheme::PIANO_ROLL_KEY_HIGHLIGHT));
        } else if (isBlackKey(note)) {
            g.setColour(accidentalKey);
        } else {
            g.setColour(key.contains(note) ? naturalKey : naturalOutOfScale);
        }
        g.fillRect(keyArea);

        if (key.isRoot(note) && noteHeight_ >= 9) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::MIDI_PLAYHEAD));
            g.setFont(FontManager::getInstance().getUIFontBold(
                juce::jmin(10.0f, static_cast<float>(noteHeight_) - 1.0f)));
            g.drawText(juce::MidiMessage::getMidiNoteName(note, true, true, 4),
                       keyArea.withTrimmedRight(4), juce::Justification::centredRight, false);
        }

        if (highlightedNotes_.find(note) != highlightedNotes_.end()) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::PIANO_ROLL_PITCH_HIGHLIGHT)
                            .withAlpha(0x55 / 255.0f));
            g.fillRect(keyArea);
        }

        // Note names live in OctaveLabelStrip now (next to the keyboard).

        // Subtle separator line between white keys
        if (!isBlackKey(note)) {
            g.setColour(keySeparator);
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

bool PianoRollKeyboard::isBlackKey(int noteNumber) {
    int note = noteNumber % 12;
    return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
}

juce::String PianoRollKeyboard::getNoteName(int noteNumber) {
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

void PianoRollKeyboard::startNote(int noteNumber) {
    currentPlayingNote_ = noteNumber;
    isPlayingNote_ = true;
    setNotePressed(noteNumber, true);
    if (onNotePreview)
        onNotePreview(noteNumber, 100, true);
}

void PianoRollKeyboard::stopNote() {
    if (!isPlayingNote_)
        return;
    if (onNotePreview)
        onNotePreview(currentPlayingNote_, 0, false);
    setNotePressed(currentPlayingNote_, false);
    isPlayingNote_ = false;
    currentPlayingNote_ = -1;
}

void PianoRollKeyboard::mouseDown(const juce::MouseEvent& event) {
    startNote(yToNoteNumber(event.y));
}

void PianoRollKeyboard::mouseDrag(const juce::MouseEvent& event) {
    // A drag across the keys plays each key it reaches; zoom lives on the zoom strip (#3429).
    const int note = yToNoteNumber(event.y);
    if (!isPlayingNote_ || note == currentPlayingNote_)
        return;
    stopNote();
    startNote(note);
}

void PianoRollKeyboard::mouseUp(const juce::MouseEvent& /*event*/) {
    stopNote();
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
        const int heightDelta = quantizedGestureStep(gesture.magnitude);
        if (heightDelta == 0)
            return;
        onZoomChanged(juce::jlimit(ClipInfo::MIN_MIDI_EDITOR_ROW_HEIGHT,
                                   ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT, noteHeight_ + heightDelta),
                      anchorNote, event.y);
        return;
    }

    if (gesture.type == GestureActionType::ScrollVertical && onScrollRequested) {
        const int scrollAmount = static_cast<int>(-gesture.magnitude);
        if (scrollAmount != 0)
            onScrollRequested(scrollAmount);
        return;
    }

    juce::Component::mouseWheelMove(event, wheel);
}

}  // namespace magda
