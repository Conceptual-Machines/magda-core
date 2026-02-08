#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "../core/TypeIds.hpp"

namespace magda {

// Forward declarations
class PluginWindowManager;
namespace te = tracktion;

/**
 * @brief Bridges AudioBridge to PluginWindowManager for plugin editor windows
 *
 * Responsibilities:
 * - Plugin window show/hide/toggle operations
 * - Window state queries (isOpen)
 * - Delegates to PluginWindowManager
 *
 * Thread Safety:
 * - All operations run on message thread (UI thread)
 * - No audio thread interaction
 */
class PluginWindowBridge {
  public:
    PluginWindowBridge() = default;
    ~PluginWindowBridge() = default;

    /**
     * @brief Set the plugin window manager (for delegation)
     * @param manager Pointer to PluginWindowManager (owned by TracktionEngineWrapper)
     */
    void setPluginWindowManager(PluginWindowManager* manager) {
        windowManager_ = manager;
    }

    /**
     * @brief Show the plugin's native editor window
     * @param plugin The Tracktion plugin
     * @param deviceId MAGDA device ID of the plugin
     */
    void showPluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin);

    /**
     * @brief Hide/close the plugin's native editor window
     * @param plugin The Tracktion plugin
     * @param deviceId MAGDA device ID of the plugin
     */
    void hidePluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin);

    /**
     * @brief Check if a plugin window is currently open
     * @param plugin The Tracktion plugin
     * @return true if the plugin window is visible
     */
    bool isPluginWindowOpen(te::Plugin::Ptr plugin) const;

    /**
     * @brief Toggle the plugin's native editor window (open if closed, close if open)
     * @param plugin The Tracktion plugin
     * @param deviceId MAGDA device ID of the plugin
     * @return true if the window is now open, false if now closed
     */
    bool togglePluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin);

    /**
     * @brief Close all windows for a specific device
     * @param deviceId MAGDA device ID
     */
    void closeWindowsForDevice(DeviceId deviceId);

  private:
    // Plugin window manager (owned by TracktionEngineWrapper, destroyed before us)
    PluginWindowManager* windowManager_ = nullptr;
};

}  // namespace magda
