#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

class NoteComponent;

class NoteGridHost {
  public:
    virtual ~NoteGridHost() = default;
    virtual double getPixelsPerBeat() const = 0;
    virtual int getNoteHeight() const = 0;
    virtual juce::Point<int> getGridScreenPosition() const = 0;
    virtual void updateNotePosition(NoteComponent* note, double beat, int noteNumber,
                                    double length) = 0;
    virtual void setCopyDragPreview(double beat, int noteNumber, double length, juce::Colour colour,
                                    bool active, size_t sourceNoteIndex) = 0;

    // Update all selected notes by a delta (for multi-note drag preview)
    virtual void updateSelectedNotePositions(NoteComponent* draggedNote, double beatDelta,
                                             int noteDelta) = 0;
    virtual void updateSelectedNoteLengths(NoteComponent* draggedNote, double lengthDelta) = 0;
    virtual void updateSelectedNoteLeftResize(NoteComponent* draggedNote, double lengthDelta) = 0;
};

}  // namespace magda
