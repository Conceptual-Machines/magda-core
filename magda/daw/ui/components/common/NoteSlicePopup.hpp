#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ClipInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Popup panel for slicing selected MIDI notes.
 *
 * Enter/Apply commits an undoable slice operation. Escape/Cancel dismisses
 * without changing the clip.
 */
class NoteSlicePopup : public juce::Component {
  public:
    NoteSlicePopup(magda::ClipId clipId, size_t noteCount);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void(int subdivisions)> onApply;

    static void showAbove(std::unique_ptr<NoteSlicePopup> popup, juce::Component* anchor);
    static void dismissCurrent();

  private:
    void apply();
    void cancel();

    static constexpr int TITLE_BAR_HEIGHT = 22;

    magda::ClipId clipId_;
    size_t noteCount_;
    juce::Label countLabel_;
    juce::Label subdivisionsLabel_;
    LinkableTextSlider subdivisionsSlider_;
    juce::TextButton applyButton_{"Apply"};
    juce::TextButton cancelButton_{"Cancel"};
    juce::ComponentDragger dragger_;

    static juce::Component::SafePointer<NoteSlicePopup> currentPopup_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteSlicePopup)
};

}  // namespace magda::daw::ui
