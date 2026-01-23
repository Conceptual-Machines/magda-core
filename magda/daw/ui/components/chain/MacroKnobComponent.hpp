#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/LinkModeManager.hpp"
#include "core/MacroInfo.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief A single macro knob with label, value slider, and link button
 *
 * Supports drag-and-drop: drag from this knob onto a ParamSlotComponent to create a link.
 *
 * Layout (vertical, ~60px wide):
 * +-----------+
 * | Macro 1   |  <- name label (editable on double-click)
 * |   0.50    |  <- value slider (0.0 to 1.0)
 * |   [Link]  |  <- link button (toggle link mode)
 * +-----------+
 *
 * Clicking the main area opens the macro editor side panel.
 * Clicking the link button enters link mode for this macro.
 */
class MacroKnobComponent : public juce::Component, public magda::LinkModeManagerListener {
  public:
    explicit MacroKnobComponent(int macroIndex);
    ~MacroKnobComponent() override;

    // Set macro info from data model
    void setMacroInfo(const magda::MacroInfo& macro);

    // Set available devices for linking (name and deviceId pairs)
    void setAvailableTargets(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Set parent path for drag-and-drop identification
    void setParentPath(const magda::ChainNodePath& path) {
        parentPath_ = path;
    }
    const magda::ChainNodePath& getParentPath() const {
        return parentPath_;
    }
    int getMacroIndex() const {
        return macroIndex_;
    }

    // Selection state
    void setSelected(bool selected);
    bool isSelected() const {
        return selected_;
    }

    // Callbacks
    std::function<void(float)> onValueChanged;
    std::function<void(magda::MacroTarget)> onTargetChanged;
    std::function<void(juce::String)> onNameChanged;
    std::function<void()> onClicked;  // Selection callback

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Drag-and-drop description prefix
    static constexpr const char* DRAG_PREFIX = "macro_drag:";

  private:
    // LinkModeManagerListener implementation
    void macroLinkModeChanged(bool active, const magda::MacroSelection& selection) override;

    void showLinkMenu();
    void paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area);
    void onNameLabelEdited();
    void onLinkButtonClicked();

    int macroIndex_;
    juce::Label nameLabel_;
    TextSlider valueSlider_{TextSlider::Format::Decimal};
    std::unique_ptr<magda::SvgButton> linkButton_;
    magda::MacroInfo currentMacro_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableTargets_;
    bool selected_ = false;
    magda::ChainNodePath parentPath_;  // For drag-and-drop identification

    // Drag state
    juce::Point<int> dragStartPos_;
    bool isDragging_ = false;
    bool isKnobDragging_ = false;  // True when dragging the knob to change value
    float dragStartValue_ = 0.0f;  // Value when knob drag started
    static constexpr int DRAG_THRESHOLD = 5;

    // Helper to get knob bounds for hit testing
    juce::Rectangle<int> getKnobBounds() const;

    static constexpr int KNOB_SIZE = 40;
    static constexpr int NAME_LABEL_HEIGHT = 11;
    static constexpr int VALUE_SLIDER_HEIGHT = 14;
    static constexpr int LINK_BUTTON_HEIGHT = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroKnobComponent)
};

}  // namespace magda::daw::ui
