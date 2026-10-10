#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/// A choice shown as adjoining segments, one per option, the selected one lit. Options that
/// name a filter response or a waveform draw it; the rest show their text.
class SegmentedChoice : public juce::Component, public juce::TooltipClient {
  public:
    void setOptions(const juce::StringArray& options);
    void setSelectedIndex(int index);
    int getSelectedIndex() const {
        return selected_;
    }
    /// A single lit-or-not segment named @p label, for an Off / On choice.
    void setToggle(const juce::String& label);
    /// Width that fits every option.
    int getPreferredWidth() const;
    static int preferredWidthFor(const juce::StringArray& options);
    /// Whether every option draws a picture; a set mixing pictures and words shows words.
    static bool allHaveIcons(const juce::StringArray& options);

    std::function<void(int index)> onChange;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

  private:
    juce::StringArray options_;
    int selected_ = -1;
    bool toggle_ = false;
    juce::Rectangle<float> segmentBounds(int index) const;
};

}  // namespace magda
