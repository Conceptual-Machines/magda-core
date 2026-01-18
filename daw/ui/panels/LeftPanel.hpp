#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magica {

class LeftPanel : public juce::Component {
  public:
    LeftPanel();
    ~LeftPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Collapse callback - called when user clicks collapse button
    std::function<void()> onCollapse;

  private:
    juce::TextButton collapseButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LeftPanel)
};

}  // namespace magica
