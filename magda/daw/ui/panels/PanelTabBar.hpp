#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "../components/common/SvgButton.hpp"
#include "content/PanelContent.hpp"
#include "state/PanelState.hpp"

namespace magda::daw::ui {

/**
 * @brief Tab bar component for TabbedPanel
 *
 * Displays horizontal row of icon buttons for switching between panel content,
 * plus an optional collapse button at one edge.
 * Sits at the bottom of the panel (footer position).
 */
class PanelTabBar : public juce::Component {
  public:
    static constexpr int MAX_TABS = 4;
    static constexpr int BUTTON_SIZE = 24;
    static constexpr int BUTTON_SPACING = 8;
    static constexpr int BAR_HEIGHT = 32;
    static constexpr int COLLAPSE_BUTTON_SIZE = 20;

    explicit PanelTabBar(PanelLocation location = PanelLocation::Bottom);
    ~PanelTabBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the tabs to display
     */
    void setTabs(const std::vector<PanelContentType>& tabs);

    /**
     * @brief Set the active tab index
     */
    void setActiveTab(int index);

    /**
     * @brief Get the current active tab index
     */
    int getActiveTab() const {
        return activeTabIndex_;
    }

    /**
     * @brief Update the collapse button icon for collapsed/expanded state
     */
    void setCollapseState(bool collapsed);

    /**
     * @brief Callback when a tab is clicked
     */
    std::function<void(int)> onTabClicked;

    /**
     * @brief Callback when collapse button is clicked
     */
    std::function<void()> onCollapseClicked;

    /**
     * @brief Callback when a tab is right-clicked (for context menu)
     */
    std::function<void(int, juce::Point<int>)> onTabRightClicked;

  private:
    PanelLocation location_;
    bool collapsed_ = false;

    std::array<std::unique_ptr<SvgButton>, MAX_TABS> tabButtons_;
    std::unique_ptr<SvgButton> collapseButton_;
    std::vector<PanelContentType> currentTabs_;
    int activeTabIndex_ = 0;

    void setupButton(size_t index, PanelContentType type);
    void setupCollapseButton();
    void updateCollapseIcon();
    void updateButtonStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelTabBar)
};

}  // namespace magda::daw::ui
