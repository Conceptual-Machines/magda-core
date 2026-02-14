#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ClipInfo.hpp"

namespace magda {

// Forward declarations
class PianoRollGridComponent;

/**
 * @brief Visual representation of a MIDI note in the piano roll
 *
 * Handles:
 * - Note rendering with velocity indicator
 * - Drag to move (horizontally and vertically)
 * - Resize handles (left/right edges)
 * - Selection
 */
class NoteComponent : public juce::Component, private juce::Timer {
  public:
    /**
     * @brief Construct a note component
     * @param noteIndex Index into the clip's midiNotes vector
     * @param parent The parent grid component
     * @param sourceClipId The ID of the clip this note belongs to
     */
    NoteComponent(size_t noteIndex, PianoRollGridComponent* parent, ClipId sourceClipId);
    ~NoteComponent() override = default;

    size_t getNoteIndex() const {
        return noteIndex_;
    }

    ClipId getSourceClipId() const {
        return sourceClipId_;
    }

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;

    // Selection state
    bool isSelected() const {
        return isSelected_;
    }
    void setSelected(bool selected);
    void setGhost(bool ghost);

    // Update note data from clip
    void updateFromNote(const MidiNote& note, juce::Colour colour);

    // Callbacks
    std::function<void(size_t)> onNoteSelected;            // noteIndex
    std::function<void(size_t, double, int)> onNoteMoved;  // noteIndex, newStartBeat, newNoteNumber
    std::function<void(size_t, double, int)> onNoteCopied;    // noteIndex, destBeat, destNoteNumber
    std::function<void(size_t, double, bool)> onNoteResized;  // noteIndex, newLength, fromStart
    std::function<void(size_t)> onNoteDeleted;                // noteIndex
    std::function<double(double)> snapBeatToGrid;             // Optional grid snapping

    // Drag preview callback - fires during drag with preview position
    std::function<void(size_t, double, bool)>
        onNoteDragging;  // noteIndex, previewStartBeat, isDragging

  private:
    size_t noteIndex_;
    ClipId sourceClipId_;
    PianoRollGridComponent* parentGrid_;
    bool isSelected_ = false;

    // Note data cache
    int noteNumber_ = 60;
    double startBeat_ = 0.0;
    double lengthBeats_ = 1.0;
    int velocity_ = 100;
    juce::Colour colour_;
    bool ghost_ = false;

    // Interaction state
    enum class DragMode { None, Move, ResizeLeft, ResizeRight };
    DragMode dragMode_ = DragMode::None;

    // Drag state
    juce::Point<int> dragStartPos_;
    double dragStartBeat_ = 0.0;
    double dragStartLength_ = 0.0;
    int dragStartNoteNumber_ = 60;

    // Preview state during drag
    double previewStartBeat_ = 0.0;
    double previewLengthBeats_ = 0.0;
    int previewNoteNumber_ = 60;
    bool isDragging_ = false;
    bool isCopyDrag_ = false;

    // Hover state for resize handles
    bool hoverLeftEdge_ = false;
    bool hoverRightEdge_ = false;

    // Visual constants
    static constexpr int RESIZE_HANDLE_WIDTH = 6;
    static constexpr int CORNER_RADIUS = 2;
    static constexpr int MIN_WIDTH_PIXELS = 8;

    // Modifier polling for cursor updates
    bool mouseIsOver_ = false;
    void timerCallback() override;

    // Interaction helpers
    bool isOnLeftEdge(int x) const;
    bool isOnRightEdge(int x) const;
    void updateCursor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteComponent)
};

}  // namespace magda
