#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ModInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A single mod cell with type icon, name, amount slider, and link indicator
 *
 * Contextual paradigm:
 * - When a param is selected, the amount slider shows the link amount for that param
 * - When no param selected, shows the mod's global/default amount
 *
 * Layout (vertical, ~60px wide):
 * +-----------+
 * | LFO 1     |  <- type + name label
 * |   0.50    |  <- amount slider (context-dependent)
 * |     *     |  <- link dot (orange if linked to selected param)
 * +-----------+
 *
 * Clicking the cell opens the modulator editor side panel.
 */
class ModKnobComponent : public juce::Component {
  public:
    explicit ModKnobComponent(int modIndex);
    ~ModKnobComponent() override = default;

    // Set mod info from data model
    void setModInfo(const magda::ModInfo& mod);

    // Set available devices for linking (name and deviceId pairs)
    void setAvailableTargets(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Contextual selection - when set, shows the link amount for this param
    void setSelectedParam(const magda::ModTarget& param);
    void clearSelectedParam();
    bool hasSelectedParam() const {
        return selectedParam_.isValid();
    }

    // Selection state (this mod cell is selected)
    void setSelected(bool selected);
    bool isSelected() const {
        return selected_;
    }

    // Callbacks
    std::function<void(float)> onAmountChanged;
    std::function<void(magda::ModTarget)> onTargetChanged;
    std::function<void(juce::String)> onNameChanged;
    std::function<void()> onClicked;  // Opens modulator editor panel
    // Link amount callbacks (for when a param is selected)
    std::function<void(magda::ModTarget, float)> onLinkAmountChanged;
    std::function<void(magda::ModTarget, float)> onNewLinkCreated;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    void showLinkMenu();
    void showAmountSlider(float currentAmount, bool isNewLink);
    void paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area);
    void onNameLabelEdited();

    int modIndex_;
    juce::Label nameLabel_;
    TextSlider amountSlider_{TextSlider::Format::Decimal};
    magda::ModInfo currentMod_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableTargets_;
    bool selected_ = false;
    magda::ModTarget selectedParam_;  // For contextual display

    void updateAmountDisplay();  // Update slider based on context

    static constexpr int NAME_LABEL_HEIGHT = 11;
    static constexpr int AMOUNT_SLIDER_HEIGHT = 14;
    static constexpr int LINK_INDICATOR_HEIGHT = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModKnobComponent)
};

}  // namespace magda::daw::ui
