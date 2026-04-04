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
 * @brief Popup panel for applying time bend to selected MIDI notes.
 *
 * Provides real-time preview: as the user drags the curve, notes move live.
 * Apply commits via command (undoable). Cancel/dismiss restores originals.
 * Designed to be shown inside a juce::CallOutBox.
 */
class TimeBendPopup : public juce::Component {
  public:
    TimeBendPopup(magda::ClipId clipId, std::vector<size_t> noteIndices);
    ~TimeBendPopup() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    /** Called when the user clicks Apply with the chosen depth, skew, cycles, quantize,
     * quantizeSub, and hardAngle. */
    std::function<void(float depth, float skew, int cycles, float quantize, int quantizeSub,
                       bool hardAngle)>
        onApply;

    /** Show as a floating window above the given component. */
    static void showAbove(std::unique_ptr<TimeBendPopup> popup, juce::Component* anchor);

  private:
    void applyPreview(float depth, float skew, int cycles, float quantize, int quantizeSub,
                      bool hardAngle);
    void restoreOriginals();

    static constexpr int TITLE_BAR_HEIGHT = 22;

    magda::ClipId clipId_;
    std::vector<size_t> noteIndices_;
    std::vector<double> originalStartBeats_;
    bool applied_ = false;

    RampCurveDisplay curveDisplay_;
    juce::Label depthLabel_;
    LinkableTextSlider depthSlider_;
    juce::Label skewLabel_;
    LinkableTextSlider skewSlider_;
    juce::Label cyclesLabel_;
    LinkableTextSlider cyclesSlider_;
    juce::Label quantizeLabel_;
    LinkableTextSlider quantizeSlider_;
    juce::Label quantizeSubLabel_;
    LinkableTextSlider quantizeSubSlider_;
    juce::TextButton applyButton_{"Apply"};
    juce::TextButton cancelButton_{"Cancel"};
    juce::ComponentDragger dragger_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeBendPopup)
};

}  // namespace magda::daw::ui
