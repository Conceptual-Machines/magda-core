#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Narrow vertical strip that draws octave labels (C-2 .. C8) next to the
 * piano roll keyboard. Independent of zoom so labels remain visible at
 * any vertical zoom level (issue #1282 follow-up).
 */
class OctaveLabelStrip : public juce::Component {
  public:
    // Visual height of each octave label, in pixels. The piano roll uses
    // this to clamp vertical zoom so labels never get clipped.
    static constexpr int LABEL_HEIGHT = 14;

    OctaveLabelStrip();
    ~OctaveLabelStrip() override = default;

    void paint(juce::Graphics& g) override;

    void setNoteHeight(int height);
    void setNoteRange(int minNote, int maxNote);
    void setScrollOffset(int offsetY);

  private:
    int noteHeight_ = 12;
    int minNote_ = 0;
    int maxNote_ = 127;
    int scrollOffsetY_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaveLabelStrip)
};

}  // namespace magda
