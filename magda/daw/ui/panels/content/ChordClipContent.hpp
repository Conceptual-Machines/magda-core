#pragma once

#include "PianoRollContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Editor for chord-track clips.
 *
 * A piano roll specialised for authoring a chord progression. The chord lane is
 * enlarged to dominate the editor (the note grid below is for tweaking each
 * chord's voicing) and the velocity/CC lanes are hidden. All MIDI-editing
 * behaviour - grid, keyboard, ruler, scrolling, chord detection - is inherited
 * unchanged from PianoRollContent; this class only flips the two chord-focus
 * extension points and reports its own content type.
 */
class ChordClipContent : public PianoRollContent, public juce::FileDragAndDropTarget {
  public:
    ChordClipContent() {
        setWantsKeyboardFocus(true);
    }

    // Delete the selected chord (not the clip) on Delete/Backspace.
    bool keyPressed(const juce::KeyPress& key) override;

    // Accept a chord dragged from the engine panel (a temp MIDI file) dropped on
    // the chord lane.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    PanelContentType getContentType() const override {
        return PanelContentType::ChordClipView;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::ChordClipView, "Chords", "Chord progression editor", "ChordClip"};
    }

    // The chord lane / note-grid divider is draggable: drag it down to give the
    // chord lane more room (all the way down hides the grid), up to reveal more
    // of the grid for editing voicings.
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    // Double-click a chord block to edit its root / quality / octave / inversion.
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    // Draws the alt-drag copy ghost + "+" badge over the lane.
    void paintOverChildren(juce::Graphics& g) override;

  protected:
    int selectedChordGroup() const override {
        return selectedGroup_;
    }
    int previewChordGroup() const override {
        return previewGroup_;
    }
    int chordRowHeight() const override {
        return laneHeight_;
    }
    bool chordFocusMode() const override {
        return true;
    }
    int sidebarWidth() const override {
        return 0;
    }
    // Show/hide grid toggle: collapse the note grid (lane fills the editor) or
    // restore it.
    void onGridToggleClicked() override;
    bool gridShown() const override {
        return laneHeight_ < maxLaneHeight() - 2;
    }
    // Clicking an empty spot on the chord lane inserts a chord (a default major
    // triad for now; quality/extensions are edited afterwards). Existing chords
    // are left alone so a stray click never stacks notes.
    bool onChordRowClicked(double clipRelativeBeat) override;

  private:
    // Insert a chord (set of MIDI pitches) at the bar nearest clipRelativeBeat,
    // then re-detect so the chord lane shows the linked block. Returns false if
    // there's no clip or that bar already has a chord.
    bool insertChordAtBeat(double clipRelativeBeat, const std::vector<int>& pitches);
    // Open the rich chord editor (CallOutBox) for the chord at annIndex.
    void openChordEditor(int annIndex);
    // Replace the chord at annIndex with the given pitches (same bar/length).
    void replaceChordNotes(int annIndex, const std::vector<int>& pitches);
    // Block ops.
    void deleteChord(int annIndex);
    void duplicateChord(int annIndex);
    void showChordContextMenu(int annIndex);
    std::vector<int> chordPitches(int annIndex) const;
    // Audition a chord block through the track instrument (same previewNote infra
    // the chord engine uses); released on mouseUp.
    void startChordPreview(int annIndex);
    void stopChordPreview();
    std::vector<int> previewNotes_;
    int previewGroup_ = 0;  // chordGroup being auditioned (for the highlight)
    bool isOnLaneDivider(juce::Point<int> p) const;
    int maxLaneHeight() const;

    // Chord-block move / resize. The block's time span IS its linked notes'
    // span, so dragging a block previews by moving the annotation and commits by
    // moving/resizing the chord's notes (the annotation then re-syncs).
    enum class BlockDrag { None, Move, ResizeLeft, ResizeRight };
    int annotationIndexAtBeat(double beat) const;
    BlockDrag dragModeForBlock(int annIndex, int mouseX) const;
    void beginBlockDrag(int annIndex, BlockDrag mode, int mouseX);
    void updateBlockDrag(int mouseX);
    void commitBlockDrag();

    static constexpr int MIN_LANE_HEIGHT = 48;
    static constexpr int DIVIDER_HIT = 4;
    static constexpr int BLOCK_EDGE_PX = 6;
    int laneHeight_ = 110;
    int expandedLaneHeight_ = 110;  // restored when un-hiding the grid
    bool draggingDivider_ = false;

    int selectedGroup_ = 0;
    bool copyDrag_ = false;  // alt-drag: copy the chord instead of moving it
    BlockDrag blockDrag_ = BlockDrag::None;
    int dragAnnIndex_ = -1;
    double dragStartMouseBeat_ = 0.0;
    double dragOrigStart_ = 0.0;
    double dragOrigEnd_ = 0.0;
    double dragNewStart_ = 0.0;
    double dragNewEnd_ = 0.0;
    struct DragNote {
        size_t index;
        double start;
        double length;
        int note;
    };
    std::vector<DragNote> dragNotes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordClipContent)
};

}  // namespace magda::daw::ui
