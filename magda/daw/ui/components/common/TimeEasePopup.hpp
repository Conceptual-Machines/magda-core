#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "core/ClipInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/RampCurveDisplay.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Popup panel for applying time ease to selected MIDI notes.
 *
 * Provides real-time preview: as the user drags the curve, notes move live.
 * Apply commits via command (undoable). Cancel/dismiss restores originals.
 * Designed to be shown inside a juce::CallOutBox.
 */
class TimeEasePopup : public juce::Component {
  public:
    TimeEasePopup(magda::ClipId clipId, std::vector<size_t> noteIndices);
    ~TimeEasePopup() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    /** Called when the user clicks Apply with the chosen depth and skew. */
    std::function<void(float depth, float skew)> onApply;

  private:
    void applyPreview(float depth, float skew);
    void restoreOriginals();

    magda::ClipId clipId_;
    std::vector<size_t> noteIndices_;
    std::vector<double> originalStartBeats_;
    bool applied_ = false;

    RampCurveDisplay curveDisplay_;
    juce::Label depthLabel_;
    LinkableTextSlider depthSlider_;
    juce::Label skewLabel_;
    LinkableTextSlider skewSlider_;
    juce::TextButton applyButton_{"Apply"};
    juce::TextButton cancelButton_{"Cancel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeEasePopup)
};

}  // namespace magda::daw::ui
