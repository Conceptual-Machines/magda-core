#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <unordered_set>

#include "core/ClipManager.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/**
 * @brief Self-contained loop-record takes section for clip inspector panels.
 *
 * Shows a take selector ("Take N / M"), an expand/collapse toggle for the
 * waveform-editor take lanes, and a Clear Comp button (when a comp is active).
 * Only relevant for a single audio clip with more than one take; getPreferredHeight()
 * returns 0 otherwise so hosts skip it.
 *
 * Used by ClipInspector and AudioClipPropertiesContent. Call setClip() or
 * setSelectedClips(), then setBounds() for layout.
 */
class ClipTakesSection : public juce::Component {
  public:
    ClipTakesSection();
    ~ClipTakesSection() override;

    /** Set a single clip (convenience wrapper for single-clip panels). */
    void setClip(magda::ClipId clipId);

    /** Set multiple selected clips (multi-edit, for ClipInspector). */
    void setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds);

    /** Height in pixels needed for the current clip. Returns 0 if there's nothing to show. */
    int getPreferredHeight() const;

    /** True when the current clip has takes worth showing (audio or MIDI). */
    bool hasContent() const {
        return hasTakes();
    }

    void resized() override;

  private:
    std::unordered_set<magda::ClipId> selectedClipIds_;

    magda::ClipId primaryClipId() const {
        return selectedClipIds_.size() == 1 ? *selectedClipIds_.begin() : magda::INVALID_CLIP_ID;
    }
    bool hasTakes() const;
    bool compActive() const;

    juce::Label sectionLabel_;
    juce::ComboBox takesCombo_;
    std::unique_ptr<magda::SvgButton> expandButton_;
    juce::TextButton clearCompButton_;

    void initControls();
    void update();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipTakesSection)
};

}  // namespace magda::daw::ui
