#pragma once

#include "../../common/DraggableValueLabel.hpp"
#include "BaseInspector.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for MIDI note properties
 *
 * Displays and edits properties of selected MIDI notes:
 * - Pitch (MIDI note number with note name display)
 * - Velocity (1-127)
 * - Start position (in beats)
 * - Length (in beats)
 * - Note count (when multiple notes selected)
 *
 * Updates via UndoManager commands to support undo/redo.
 */
class NoteInspector : public BaseInspector {
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
    juce::Label noteStartValue_;
    juce::Label noteLengthLabel_;
    std::unique_ptr<magda::DraggableValueLabel> noteLengthValue_;

    // Update methods
    void updateFromSelectedNotes();
    void showNoteControls(bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteInspector)
};

}  // namespace magda::daw::ui
