#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/// A choice shown as adjoining segments, one per option, the selected one lit.
class SegmentedChoice : public juce::Component {
  public:
    void setOptions(const juce::StringArray& options);
    void setSelectedIndex(int index);
    int getSelectedIndex() const {
        return selected_;
    }
    /// Width that fits every option at its text width.
    int getPreferredWidth() const;

    std::function<void(int index)> onChange;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    juce::StringArray options_;
    int selected_ = -1;
    juce::Rectangle<float> segmentBounds(int index) const;
};

}  // namespace magda
