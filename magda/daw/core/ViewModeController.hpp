#pragma once

#include <vector>

#include "ViewModeEvents.hpp"
#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief Listener interface for view mode changes
 */
class ViewModeListener {
  public:
    virtual ~ViewModeListener() = default;

    // Interface classes shouldn't be copied or moved
    ViewModeListener(const ViewModeListener&) = delete;
    ViewModeListener& operator=(const ViewModeListener&) = delete;
    ViewModeListener(ViewModeListener&&) = delete;
    ViewModeListener& operator=(ViewModeListener&&) = delete;

    /**
     * Called when the view mode changes.
     */
    virtual void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) = 0;

  protected:
    // Allow derived classes to construct
    ViewModeListener() = default;
};

/**
 * @brief Central controller for view mode state management
 *
 * Singleton pattern for global access to view mode state.
 * Manages view mode changes and notifies listeners.
 */
class ViewModeController {
  public:
    /**
     * Get the singleton instance.
     */
    static ViewModeController& getInstance();

    // Delete copy/move operations
    ViewModeController(const ViewModeController&) = delete;
    ViewModeController& operator=(const ViewModeController&) = delete;
    ViewModeController(ViewModeController&&) = delete;
    ViewModeController& operator=(ViewModeController&&) = delete;

    // ===== State Access =====

    /**
     * Get the current view mode.
     */
    ViewMode getViewMode() const {
        return currentMode;
    }

    /**
     * Get the audio engine profile for the current mode.
     */
    AudioEngineProfile getAudioProfile() const {
        return AudioEngineProfile::getProfileForMode(currentMode);
    }

    // ===== Event Dispatching =====

    /**
     * Dispatch an event to modify the view mode state.
     */
    void dispatch(const ViewModeEvent& event);

    /**
     * Convenience method to set the view mode directly.
     */
    void setViewMode(ViewMode mode);

    // ===== Listener Management =====

    /**
     * Add a listener to receive view mode change notifications.
     */
    void addListener(ViewModeListener* listener);

    /**
     * Remove a previously added listener.
     */
    void removeListener(ViewModeListener* listener);

  private:
    ViewModeController();
    ~ViewModeController() = default;

    ViewMode currentMode = ViewMode::Arrange;  // Default to Arrange mode
    std::vector<ViewModeListener*> listeners;

    void notifyListeners();
};

}  // namespace magda
