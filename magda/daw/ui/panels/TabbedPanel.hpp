#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <unordered_map>

#include "../components/common/SvgButton.hpp"
#include "PanelTabBar.hpp"
#include "content/PanelContent.hpp"
#include "content/PanelContentFactory.hpp"
#include "state/PanelController.hpp"

namespace magda {
class AudioEngine;
class TimelineController;
}  // namespace magda

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
    void mouseDoubleClick(const juce::MouseEvent& event) override;

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

    /**
     * @brief Set the audio engine reference for panels that need it
     * This reference will be passed to content that implements setAudioEngine()
     */
    void setAudioEngine(magda::AudioEngine* engine);

    /**
     * @brief Set the timeline controller reference for panels that need it
     * This reference will be passed to content that implements setTimelineController()
     */
    void setTimelineController(magda::TimelineController* controller);

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
     * @brief Hook called just before content switches
     * Override to manage header population/depopulation.
     */
    virtual void onContentWillSwitch(PanelContent* outgoing, PanelContent* incoming) {
        juce::ignoreUnused(outgoing, incoming);
    }

    /**
     * @brief Get the bounds for the tab bar
     */
    virtual juce::Rectangle<int> getTabBarBounds();

  private:
    PanelLocation location_;
    bool collapsed_ = false;

    PanelTabBar tabBar_;

    // Expand button shown only when panel is collapsed (thin bar state)
    std::unique_ptr<magda::SvgButton> expandButton_;

    // Cache of content instances (lazy creation)
    std::unordered_map<PanelContentType, std::unique_ptr<PanelContent>> contentCache_;
    PanelContent* activeContent_ = nullptr;

    // References to pass to content (non-owning)
    magda::AudioEngine* audioEngine_ = nullptr;
    magda::TimelineController* timelineController_ = nullptr;

    void setupExpandButton();
    void updateFromState();
    void switchToContent(PanelContentType type);
    PanelContent* getOrCreateContent(PanelContentType type);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TabbedPanel)
};

}  // namespace magda::daw::ui
