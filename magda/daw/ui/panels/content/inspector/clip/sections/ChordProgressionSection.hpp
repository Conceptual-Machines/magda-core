#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <unordered_set>

#include "core/ClipManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Progression view for a chord-track clip in the inspector.
 *
 * Lists each chord in the clip (bar, name, length) in the active C / Do
 * notation. getPreferredHeight() returns 0 for non-chord-track clips so the host
 * skips it. Read-only; chords are edited on the chord lane.
 */
class ChordProgressionSection : public juce::Component, private juce::ChangeListener {
  public:
    ChordProgressionSection();
    ~ChordProgressionSection() override;

    void setClip(magda::ClipId clipId);
    void setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds);

    int getPreferredHeight() const;
    bool hasContent() const {
        return getPreferredHeight() > 0;
    }

    void paint(juce::Graphics& g) override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        repaint();
    }
    bool isChordClip() const;

    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;

    static constexpr int HEADER_H = 20;
    static constexpr int ROW_H = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordProgressionSection)
};

}  // namespace magda::daw::ui
