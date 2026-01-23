#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/LinkModeManager.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A single mod cell with type icon, name, amount slider, and link button
 *
 * Supports drag-and-drop: drag from this knob onto a ParamSlotComponent to create a link.
 *
 * Layout (vertical, ~60px wide):
 * +-----------+
 * | LFO 1     |  <- type + name label
 * |   0.50    |  <- amount slider
 * |   [Link]  |  <- link button (toggle link mode)
 * +-----------+
 *
 * Clicking the main area opens the modulator editor side panel.
 * Clicking the link button enters link mode for this mod.
 */
class ModKnobComponent : public juce::Component, public magda::LinkModeManagerListener {
  public:
    explicit ModKnobComponent(int modIndex);
    ~ModKnobComponent() override;

    // Set mod info from data model
    void setModInfo(const magda::ModInfo& mod);

    // Set available devices for linking (name and deviceId pairs)
    void setAvailableTargets(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Set parent path for drag-and-drop identification
    void setParentPath(const magda::ChainNodePath& path) {
        parentPath_ = path;
    }
    const magda::ChainNodePath& getParentPath() const {
        return parentPath_;
    }
    int getModIndex() const {
        return modIndex_;
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
    std::function<void(magda::LFOWaveform)> onWaveformChanged;
    std::function<void(float)> onRateChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Drag-and-drop description prefix
    static constexpr const char* DRAG_PREFIX = "mod_drag:";

  private:
    // LinkModeManagerListener implementation
    void modLinkModeChanged(bool active, const magda::ModSelection& selection) override;

    void showLinkMenu();
    void paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area);
    void onNameLabelEdited();
    void onLinkButtonClicked();

    int modIndex_;
    juce::Label nameLabel_;
    TextSlider amountSlider_{TextSlider::Format::Decimal};
    juce::ComboBox waveformCombo_;
    TextSlider rateSlider_{TextSlider::Format::Decimal};
    std::unique_ptr<magda::SvgButton> linkButton_;
    magda::ModInfo currentMod_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableTargets_;
    bool selected_ = false;
    magda::ChainNodePath parentPath_;  // For drag-and-drop identification

    // Drag state
    juce::Point<int> dragStartPos_;
    bool isDragging_ = false;
    static constexpr int DRAG_THRESHOLD = 5;

    static constexpr int NAME_LABEL_HEIGHT = 11;
    static constexpr int AMOUNT_SLIDER_HEIGHT = 14;
    static constexpr int WAVEFORM_COMBO_HEIGHT = 12;
    static constexpr int RATE_SLIDER_HEIGHT = 14;
    static constexpr int LINK_BUTTON_HEIGHT = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModKnobComponent)
};

}  // namespace magda::daw::ui
