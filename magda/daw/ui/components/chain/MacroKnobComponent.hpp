#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/MacroInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A single macro knob with label, value slider, and link indicator
 *
 * Layout (vertical, ~60px wide):
 * +-----------+
 * | Macro 1   |  <- name label (editable on double-click)
 * |   0.50    |  <- value slider (0.0 to 1.0)
 * |     *     |  <- link dot (purple if linked)
 * +-----------+
 */
class MacroKnobComponent : public juce::Component {
  public:
    explicit MacroKnobComponent(int macroIndex);
    ~MacroKnobComponent() override = default;

    // Set macro info from data model
    void setMacroInfo(const magda::MacroInfo& macro);

    // Set available devices for linking (name and deviceId pairs)
    void setAvailableTargets(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Callbacks
    std::function<void(float)> onValueChanged;
    std::function<void(magda::MacroTarget)> onTargetChanged;
    std::function<void(juce::String)> onNameChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    void showLinkMenu();
    void paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area);
    void onNameLabelEdited();

    int macroIndex_;
    juce::Label nameLabel_;
    TextSlider valueSlider_{TextSlider::Format::Decimal};
    magda::MacroInfo currentMacro_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableTargets_;

    static constexpr int NAME_LABEL_HEIGHT = 11;
    static constexpr int VALUE_SLIDER_HEIGHT = 14;
    static constexpr int LINK_INDICATOR_HEIGHT = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroKnobComponent)
};

}  // namespace magda::daw::ui
