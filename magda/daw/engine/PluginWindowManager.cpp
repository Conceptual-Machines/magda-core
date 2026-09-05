#include "PluginWindowManager.hpp"

#include <iostream>

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
    try {
        closeAllWindows();
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[PluginWindowManager] ") + e.what());
    } catch (...) {
        juce::Logger::writeToLog("[PluginWindowManager] unknown exception during teardown");
    }

    DBG("PluginWindowManager destroyed");
}

// =============================================================================
// Window Control
// =============================================================================

void PluginWindowManager::showPluginWindow(DeviceId deviceId, const te::Plugin::Ptr& plugin) {
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
            DBG("  -> Calling showWindowExplicitly() for: " << extPlugin->getName());
            extPlugin->windowState->showWindowExplicitly();

            bool showing = extPlugin->windowState->isWindowShowing();
            DBG("  -> After showWindowExplicitly, isWindowShowing=" << (showing ? "true"
                                                                                : "false"));

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

void PluginWindowManager::hidePluginWindow(DeviceId deviceId, const te::Plugin::Ptr& plugin) {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    if (!plugin) {
        return;
    }

    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        if (extPlugin->windowState) {
            DBG("PluginWindowManager::hidePluginWindow - closing window for: "
                << extPlugin->getName());
            // Use Tracktion's API to properly close the window.
            // This is safe now that we use JUCE's title bar (not native macOS).
            extPlugin->windowState->closeWindowExplicitly();

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

bool PluginWindowManager::togglePluginWindow(DeviceId deviceId, const te::Plugin::Ptr& plugin) {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return false;
    }

    if (isPluginWindowOpen(plugin)) {
        hidePluginWindow(deviceId, plugin);
        return false;
    } else {
        showPluginWindow(deviceId, plugin);
        return true;
    }
}

bool PluginWindowManager::isPluginWindowOpen(const te::Plugin::Ptr& plugin) const {
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return false;
    }

    if (!plugin) {
        return false;
    }

    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        if (extPlugin->windowState) {
            return extPlugin->windowState->isWindowShowing();
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

    // Track window state changes and notify listeners
    // Close handling is now done directly in PluginEditorWindow::closeButtonPressed()
    // via state_.closeWindowExplicitly() since we use JUCE's title bar (not native).
    std::vector<std::pair<DeviceId, bool>> stateChanges;

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

            // Track window state changes
            bool currentlyShowing = extPlugin->windowState->isWindowShowing();
            if (!info.wasOpen && currentlyShowing) {
                info.wasOpen = true;
                stateChanges.push_back({deviceId, true});
            } else if (info.wasOpen && !currentlyShowing) {
                // Window was closed (via closeButtonPressed or other means)
                info.wasOpen = false;
                stateChanges.push_back({deviceId, false});
            }
        }
    }

    // Notify about state changes
    for (const auto& [deviceId, isOpen] : stateChanges) {
        if (onWindowStateChanged) {
            onWindowStateChanged(deviceId, isOpen);
        }
    }
}

}  // namespace magda
