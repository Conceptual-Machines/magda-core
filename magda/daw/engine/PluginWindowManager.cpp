#include "PluginWindowManager.hpp"

#include <iostream>

#include "MagdaUIBehaviour.hpp"

namespace magda {

PluginWindowManager::PluginWindowManager(te::Engine& engine, te::Edit& edit)
    : engine_(engine), edit_(edit) {
    // Start timer at 10Hz to detect hidden windows
    startTimerHz(10);
    DBG("PluginWindowManager initialized");
}

PluginWindowManager::~PluginWindowManager() {
    DBG("PluginWindowManager::~PluginWindowManager - starting cleanup");

    // Set shutdown flag FIRST
    isShuttingDown_.store(true, std::memory_order_release);

    // Stop timer immediately
    stopTimer();

    // Close all remaining windows
    closeAllWindows();

    DBG("PluginWindowManager destroyed");
}

// =============================================================================
// Window Control
// =============================================================================

void PluginWindowManager::showPluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin) {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    if (!plugin) {
        DBG("PluginWindowManager::showPluginWindow - plugin is null for deviceId=" << deviceId);
        return;
    }

    DBG("PluginWindowManager::showPluginWindow - deviceId="
        << deviceId << " thread="
        << (juce::MessageManager::getInstance()->isThisTheMessageThread() ? "message" : "other"));

    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        if (extPlugin->windowState) {
            // If window exists but is hidden, just make it visible again
            if (extPlugin->windowState->pluginWindow &&
                !extPlugin->windowState->pluginWindow->isVisible()) {
                DBG("  -> Making hidden window visible for: " << extPlugin->getName());
                extPlugin->windowState->pluginWindow->setVisible(true);
                extPlugin->windowState->pluginWindow->toFront(true);
            } else {
                // No window or window is already visible - use showWindowExplicitly
                DBG("  -> Calling showWindowExplicitly() for: " << extPlugin->getName());
                extPlugin->windowState->showWindowExplicitly();
            }

            bool showing = extPlugin->windowState->isWindowShowing();
            DBG("  -> After show, isWindowShowing=" << (showing ? "true" : "false"));

            // Track this window
            {
                juce::ScopedLock lock(windowLock_);
                trackedWindows_[deviceId] = {plugin, showing};
            }

            if (onWindowStateChanged) {
                onWindowStateChanged(deviceId, showing);
            }
        } else {
            DBG("  -> Plugin has no windowState: " << extPlugin->getName());
        }
    } else {
        DBG("  -> Plugin is not external, no window to show: " << plugin->getName());
    }
}

void PluginWindowManager::hidePluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin) {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    if (!plugin) {
        return;
    }

    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        if (extPlugin->windowState && extPlugin->windowState->pluginWindow) {
            DBG("PluginWindowManager::hidePluginWindow - hiding window for: "
                << extPlugin->getName());
            // Just hide the window instead of destroying it to avoid malloc errors
            // The window will be properly destroyed when the plugin is unloaded
            extPlugin->windowState->pluginWindow->setVisible(false);

            // Update tracking
            {
                juce::ScopedLock lock(windowLock_);
                auto it = trackedWindows_.find(deviceId);
                if (it != trackedWindows_.end()) {
                    it->second.wasOpen = false;
                }
            }

            if (onWindowStateChanged) {
                onWindowStateChanged(deviceId, false);
            }
        }
    }
}

bool PluginWindowManager::togglePluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin) {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return false;
    }

    if (isPluginWindowOpen(deviceId, plugin)) {
        hidePluginWindow(deviceId, plugin);
        return false;
    } else {
        showPluginWindow(deviceId, plugin);
        return true;
    }
}

bool PluginWindowManager::isPluginWindowOpen(DeviceId deviceId, te::Plugin::Ptr plugin) const {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return false;
    }

    if (!plugin) {
        return false;
    }

    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        if (extPlugin->windowState && extPlugin->windowState->pluginWindow) {
            // Check actual visibility of the window component
            return extPlugin->windowState->pluginWindow->isVisible();
        }
    }
    return false;
}

// =============================================================================
// Bulk Operations
// =============================================================================

void PluginWindowManager::closeAllWindows() {
    DBG("PluginWindowManager::closeAllWindows");

    // Collect windows to close
    std::vector<std::pair<DeviceId, te::Plugin::Ptr>> windowsToClose;
    {
        juce::ScopedLock lock(windowLock_);
        for (const auto& [deviceId, info] : trackedWindows_) {
            if (info.plugin) {
                windowsToClose.push_back({deviceId, info.plugin});
            }
        }
    }

    // Close each window (outside the lock to avoid deadlocks)
    for (const auto& [deviceId, plugin] : windowsToClose) {
        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            if (extPlugin->windowState && extPlugin->windowState->isWindowShowing()) {
                DBG("  -> Closing window for device " << deviceId << ": " << extPlugin->getName());
                extPlugin->windowState->closeWindowExplicitly();
            }
        }
    }

    // Clear tracking
    {
        juce::ScopedLock lock(windowLock_);
        trackedWindows_.clear();
    }
}

void PluginWindowManager::closeWindowsForDevice(DeviceId deviceId) {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    te::Plugin::Ptr plugin;
    {
        juce::ScopedLock lock(windowLock_);
        auto it = trackedWindows_.find(deviceId);
        if (it != trackedWindows_.end()) {
            plugin = it->second.plugin;
            trackedWindows_.erase(it);
        }
    }

    if (plugin) {
        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            if (extPlugin->windowState && extPlugin->windowState->isWindowShowing()) {
                DBG("PluginWindowManager::closeWindowsForDevice - closing window for device "
                    << deviceId);
                extPlugin->windowState->closeWindowExplicitly();
            }
        }
    }
}

// =============================================================================
// Timer Callback
// =============================================================================

void PluginWindowManager::timerCallback() {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    // Check for windows that have requested close (user clicked X)
    std::vector<std::pair<DeviceId, te::Plugin::Ptr>> windowsToClose;

    {
        juce::ScopedLock lock(windowLock_);
        for (auto& [deviceId, info] : trackedWindows_) {
            if (!info.plugin) {
                continue;
            }

            auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(info.plugin.get());
            if (!extPlugin || !extPlugin->windowState) {
                continue;
            }

            // Check if this window has the closeRequested flag set
            // We need to find the actual window component to check the flag
            if (extPlugin->windowState->isWindowShowing()) {
                // The window might be a PluginEditorWindow - check for close request
                if (auto* window = extPlugin->windowState->pluginWindow.get()) {
                    if (auto* editorWindow = dynamic_cast<PluginEditorWindow*>(window)) {
                        if (editorWindow->isCloseRequested()) {
                            DBG("PluginWindowManager::timerCallback - close requested for device "
                                << deviceId);
                            editorWindow->clearCloseRequest();
                            windowsToClose.push_back({deviceId, info.plugin});
                            info.wasOpen = false;
                        }
                    }
                }
            }

            // Also track window state changes for other purposes
            bool currentlyShowing = extPlugin->windowState->isWindowShowing();
            if (!info.wasOpen && currentlyShowing) {
                info.wasOpen = true;
            } else if (info.wasOpen && !currentlyShowing) {
                // Window was closed some other way - update tracking
                info.wasOpen = false;
            }
        }
    }

    // Handle windows that requested close - just hide them instead of destroying
    // This avoids the malloc error that occurs when closeWindowExplicitly() is called.
    // The window stays alive in memory but hidden until the plugin is unloaded.
    for (const auto& [deviceId, plugin] : windowsToClose) {
        // Capture plugin ptr (ref counted) to keep it alive during async call
        te::Plugin::Ptr pluginCopy = plugin;
        auto deviceIdCopy = deviceId;
        auto callback = onWindowStateChanged;

        juce::MessageManager::callAsync([pluginCopy, deviceIdCopy, callback]() {
            if (pluginCopy) {
                if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(pluginCopy.get())) {
                    if (extPlugin->windowState && extPlugin->windowState->pluginWindow) {
                        DBG("PluginWindowManager - hiding window for device " << deviceIdCopy);
                        // Just hide the window instead of destroying it
                        // This allows re-showing with showWindowExplicitly()
                        extPlugin->windowState->pluginWindow->setVisible(false);
                    }
                }
            }

            if (callback) {
                callback(deviceIdCopy, false);
            }
        });
    }
}

}  // namespace magda
