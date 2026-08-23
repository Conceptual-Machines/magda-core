#pragma once

#include <string>
#include <vector>

#include "PanelEvents.hpp"
#include "PanelState.hpp"

namespace magda::daw::ui {

/**
 * @brief Listener interface for panel state changes
 */
class PanelStateListener {
  public:
    virtual ~PanelStateListener() = default;

    /**
     * @brief Called when any panel state changes
     */
    virtual void panelStateChanged(PanelLocation location, const PanelState& state) = 0;

    /**
     * @brief Called when a panel's active tab changes
     */
    virtual void activeTabChanged(PanelLocation location, int tabIndex,
                                  PanelContentType contentType) {
        juce::ignoreUnused(location, tabIndex, contentType);
    }

    /**
     * @brief Called when a panel's collapsed state changes
     */
    virtual void panelCollapsedChanged(PanelLocation location, bool collapsed) {
        juce::ignoreUnused(location, collapsed);
    }
};

/**
 * @brief Reorder a panel's tabs to follow a persisted order.
 *
 * Content-type ids this build does not know, and ids for tabs the panel does
 * not offer, are dropped; tabs the saved order never mentions are appended in
 * their default position. A layout written by a build with a different tab set
 * therefore still opens, with every tab this build has.
 *
 * An empty saved order leaves the panel untouched.
 */
void applyPersistedTabOrder(PanelState& panel, const std::vector<std::string>& savedOrder);

/**
 * @brief Bring a persisted front tab forward, if this build still has that tab.
 *
 * An empty or unrecognised id leaves the panel's default front tab in place.
 */
void applyPersistedActiveTab(PanelState& panel, const std::string& savedActiveTab);

/**
 * @brief Singleton controller for managing panel state
 *
 * Follows the ViewModeController pattern. Manages the state of all panels,
 * dispatches events, and notifies listeners of state changes.
 */
class PanelController {
  public:
    /**
     * @brief Get the singleton instance
     */
    static PanelController& getInstance();

    // Delete copy/move operations for singleton
    PanelController(const PanelController&) = delete;
    PanelController& operator=(const PanelController&) = delete;
    PanelController(PanelController&&) = delete;
    PanelController& operator=(PanelController&&) = delete;

    /**
     * @brief Get the complete state of all panels
     */
    const AllPanelStates& getState() const {
        return state_;
    }

    /**
     * @brief Get state for a specific panel
     */
    const PanelState& getPanelState(PanelLocation location) const {
        return state_.getPanel(location);
    }

    /**
     * @brief Dispatch an event to update panel state
     */
    void dispatch(const PanelEvent& event);

    // Convenience methods for common operations

    /**
     * @brief Set the active tab in a panel by index
     */
    void setActiveTab(PanelLocation panel, int tabIndex);

    /**
     * @brief Set the active tab in a panel by content type
     */
    void setActiveTabByType(PanelLocation panel, PanelContentType contentType);

    /**
     * @brief Toggle a panel's collapsed state
     */
    void toggleCollapsed(PanelLocation panel);

    /**
     * @brief Set a panel's collapsed state
     */
    void setCollapsed(PanelLocation panel, bool collapsed);

    /**
     * @brief Reset all panels to default configuration
     */
    void resetToDefaults();

    // Listener management

    /**
     * @brief Add a listener to receive state change notifications
     */
    void addListener(PanelStateListener* listener);

    /**
     * @brief Remove a listener
     */
    void removeListener(PanelStateListener* listener);

  private:
    PanelController();
    ~PanelController() = default;

    AllPanelStates state_;
    std::vector<PanelStateListener*> listeners_;

    /** Overlay the tab order and front tab persisted in Config onto the
     *  defaults already in state_. Anything the config does not name, or
     *  names with an id this build does not know, keeps its default. */
    void restoreFromConfig();

    /** Write this panel's tab order and front tab back to Config and flush.
     *  No-op when nothing the config holds has actually changed, so the
     *  selection-driven bottom panel does not write on every clip click. */
    void persistToConfig(PanelLocation location);

    void notifyPanelChanged(PanelLocation location);
    void notifyActiveTabChanged(PanelLocation location);
    void notifyCollapsedChanged(PanelLocation location);

    void handleSetActiveTab(const SetActiveTabEvent& event);
    void handleSetActiveTabByType(const SetActiveTabByTypeEvent& event);
    void handleToggleCollapsed(const TogglePanelCollapsedEvent& event);
    void handleSetCollapsed(const SetPanelCollapsedEvent& event);
    void handleSetSize(const SetPanelSizeEvent& event);
    void handleAddTab(const AddTabEvent& event);
    void handleRemoveTab(const RemoveTabEvent& event);
    void handleReorderTabs(const ReorderTabsEvent& event);
    void handleResetToDefaults(const ResetPanelsToDefaultEvent& event);
};

}  // namespace magda::daw::ui
