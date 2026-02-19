#pragma once

#include "../../common/DraggableValueLabel.hpp"
#include "BaseInspector.hpp"
#include "core/ClipManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for MIDI note properties
 *
 * Displays and edits properties of selected MIDI notes in a 2-column layout:
 *   Pitch | Velocity
 *   Start | Length
 *
 * All four properties are draggable. For multi-selection, shows property
 * ranges and allows relative delta-based adjustments via dragging.
 *
 * Updates via UndoManager commands to support undo/redo.
 * Listens to ClipManager for realtime data updates.
 */
class NoteInspector : public BaseInspector, private magda::ClipManagerListener {
  public:
    NoteInspector();
    ~NoteInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the currently selected notes
     * @param selection Note selection (can be empty, single, or multiple notes)
     */
    void setSelectedNotes(const magda::NoteSelection& selection);

  private:
    // Current selection
    magda::NoteSelection noteSelection_;

    // Note properties
    juce::Label noteCountLabel_;
    juce::Label notePitchLabel_;
    std::unique_ptr<magda::DraggableValueLabel> notePitchValue_;
    juce::Label noteVelocityLabel_;
    std::unique_ptr<magda::DraggableValueLabel> noteVelocityValue_;
    juce::Label noteStartLabel_;
    std::unique_ptr<magda::DraggableValueLabel> noteStartValue_;
    juce::Label noteLengthLabel_;
    std::unique_ptr<magda::DraggableValueLabel> noteLengthValue_;

    // Multi-selection range cache
    struct NoteRange {
        bool valid = false;  // True if at least one note was processed
        int minPitch = 0, maxPitch = 0;
        int minVelocity = 0, maxVelocity = 0;
        double minLength = 0.0, maxLength = 0.0;
        double minStart = 0.0, maxStart = 0.0;
    };
    NoteRange multiRange_;

    // Drag-start tracking for multi-selection delta edits
    double multiPitchDragStart_ = 0.0;
    double multiVelocityDragStart_ = 0.0;
    double multiStartDragStart_ = 0.0;
    double multiLengthDragStart_ = 0.0;

    // Update methods
    void updateFromSelectedNotes();
    void computeMultiRange();
    void showNoteControls(bool show);

    // ClipManagerListener
    void clipsChanged() override {}
    void clipPropertyChanged(magda::ClipId clipId) override;

    // Helpers
    static juce::String midiNoteToName(int noteNumber);
    void refreshMultiRangeDisplay();
    void refreshDisplayValues();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteInspector)
};

}  // namespace magda::daw::ui
