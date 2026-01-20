#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Piano keyboard component for the piano roll.
 * Displays note names and responds to vertical scroll offset.
 */
class PianoRollKeyboard : public juce::Component {
  public:
    PianoRollKeyboard();
    ~PianoRollKeyboard() override = default;

    void paint(juce::Graphics& g) override;

    // Configuration
    void setNoteHeight(int height);
    void setNoteRange(int minNote, int maxNote);
    void setScrollOffset(int offsetY);

    int getNoteHeight() const {
        return noteHeight_;
    }

  private:
    int noteHeight_ = 12;
    int minNote_ = 21;   // A0
    int maxNote_ = 108;  // C8
    int scrollOffsetY_ = 0;

    bool isBlackKey(int noteNumber) const;
    juce::String getNoteName(int noteNumber) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollKeyboard)
};

}  // namespace magda
