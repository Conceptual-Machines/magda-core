#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../layout/LayoutConfig.hpp"

namespace magica {

/**
 * Debug panel for adjusting LayoutConfig values in real-time.
 * Press F11 to toggle visibility.
 */
class LayoutDebugPanel : public juce::Component {
  public:
    LayoutDebugPanel();
    ~LayoutDebugPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Callback when any value changes
    std::function<void()> onLayoutChanged;

  private:
    struct SliderRow {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        int* valuePtr;
    };

    std::vector<SliderRow> rows;

    void addSlider(const juce::String& name, int* valuePtr, int min, int max);
    void updateFromConfig();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayoutDebugPanel)
};

}  // namespace magica
