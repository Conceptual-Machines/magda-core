#include "PluginWindowBridge.hpp"

#include "../engine/PluginWindowManager.hpp"

namespace magda {

void PluginWindowBridge::showPluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin) {
    if (windowManager_ && plugin) {
        windowManager_->showPluginWindow(deviceId, plugin);
    }
}

void PluginWindowBridge::hidePluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin) {
    if (windowManager_ && plugin) {
        windowManager_->hidePluginWindow(deviceId, plugin);
    }
}

bool PluginWindowBridge::isPluginWindowOpen(te::Plugin::Ptr plugin) const {
    if (windowManager_ && plugin) {
        return windowManager_->isPluginWindowOpen(plugin);
    }
    return false;
}

bool PluginWindowBridge::togglePluginWindow(DeviceId deviceId, te::Plugin::Ptr plugin) {
    if (windowManager_ && plugin) {
        return windowManager_->togglePluginWindow(deviceId, plugin);
    }
    return false;
}

void PluginWindowBridge::closeWindowsForDevice(DeviceId deviceId) {
    if (windowManager_) {
        windowManager_->closeWindowsForDevice(deviceId);
    }
}

}  // namespace magda
