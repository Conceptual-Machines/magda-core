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
class ChordClipContent : public PianoRollContent {
  public:
    ChordClipContent() = default;

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

  protected:
    int chordRowHeight() const override {
        return laneHeight_;
    }
    bool chordFocusMode() const override {
        return true;
    }
    int sidebarWidth() const override {
        return 0;
    }
    // Clicking an empty spot on the chord lane inserts a chord (a default major
    // triad for now; quality/extensions are edited afterwards). Existing chords
    // are left alone so a stray click never stacks notes.
    bool onChordRowClicked(double clipRelativeBeat) override;

  private:
    bool isOnLaneDivider(juce::Point<int> p) const;
    int maxLaneHeight() const;

    static constexpr int MIN_LANE_HEIGHT = 48;
    static constexpr int DIVIDER_HIT = 4;
    int laneHeight_ = 110;
    bool draggingDivider_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordClipContent)
};

}  // namespace magda::daw::ui
