#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magica {

class RightPanel : public juce::Component {
  public:
    RightPanel();
    ~RightPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Collapsed state
    void setCollapsed(bool collapsed);
    bool isCollapsed() const {
        return collapsed_;
    }

    // Collapse/expand callback - called when user clicks the button
    std::function<void(bool)> onCollapseChanged;  // passes new collapsed state

  private:
    bool collapsed_ = false;
    juce::TextButton collapseButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RightPanel)
};

}  // namespace magica
