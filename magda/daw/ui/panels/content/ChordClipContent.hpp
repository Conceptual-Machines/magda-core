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

  protected:
    int chordRowHeight() const override {
        return 72;
    }
    bool chordFocusMode() const override {
        return true;
    }
    // Clicking an empty spot on the chord lane inserts a chord (a default major
    // triad for now; quality/extensions are edited afterwards). Existing chords
    // are left alone so a stray click never stacks notes.
    bool onChordRowClicked(double clipRelativeBeat) override;

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordClipContent)
};

}  // namespace magda::daw::ui
