#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <unordered_map>

#include "../components/common/SvgButton.hpp"
#include "PanelTabBar.hpp"
#include "content/PanelContent.hpp"
#include "content/PanelContentFactory.hpp"
#include "state/PanelController.hpp"

namespace magda::daw::ui {

/**
 * @brief Base class for tabbed panels (LeftPanel, RightPanel, BottomPanel)
 *
 * Manages a tab bar and content switching. Content instances are created
 * lazily via PanelContentFactory and cached for reuse.
 * Listens to PanelController for state changes.
 */
class TabbedPanel : public juce::Component, public PanelStateListener {
  public:
    explicit TabbedPanel(PanelLocation location);
    ~TabbedPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // PanelStateListener interface
    void panelStateChanged(PanelLocation location, const PanelState& state) override;
    void activeTabChanged(PanelLocation location, int tabIndex,
                          PanelContentType contentType) override;
    void panelCollapsedChanged(PanelLocation location, bool collapsed) override;

    /**
     * @brief Get the panel location
     */
    PanelLocation getLocation() const {
        return location_;
    }

    /**
     * @brief Check if the panel is collapsed
     */
    bool isCollapsed() const {
        return collapsed_;
    }

    /**
     * @brief Callback when collapse state changes
     */
    std::function<void(bool)> onCollapseChanged;

  protected:
    /**
     * @brief Override to customize background painting
     */
    virtual void paintBackground(juce::Graphics& g);

    /**
     * @brief Override to customize border painting
     */
    virtual void paintBorder(juce::Graphics& g);

    /**
     * @brief Get the bounds for the content area
     */
    virtual juce::Rectangle<int> getContentBounds();

    /**
     * @brief Get the currently active content
     */
    PanelContent* getActiveContent() const {
        return activeContent_;
    }

    /**
     * @brief Get the bounds for the tab bar
     */
    virtual juce::Rectangle<int> getTabBarBounds();

    /**
     * @brief Get the bounds for the collapse button
     */
    virtual juce::Rectangle<int> getCollapseButtonBounds();

    /**
     * @brief Get the collapse button text for current state
     */
    virtual juce::String getCollapseButtonText() const;

  private:
    PanelLocation location_;
    bool collapsed_ = false;

    PanelTabBar tabBar_;
    std::unique_ptr<magda::SvgButton> collapseButton_;
    juce::TextButton collapseFallbackButton_;  // Fallback if no SVG

    // Cache of content instances (lazy creation)
    std::unordered_map<PanelContentType, std::unique_ptr<PanelContent>> contentCache_;
    PanelContent* activeContent_ = nullptr;

    void setupCollapseButton();
    void updateFromState();
    void switchToContent(PanelContentType type);
    PanelContent* getOrCreateContent(PanelContentType type);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TabbedPanel)
};

}  // namespace magda::daw::ui
