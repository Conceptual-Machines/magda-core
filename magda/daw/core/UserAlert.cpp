// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UserAlert.hpp"

#include <utility>

namespace magda {

namespace {
UserAlertHandler& handler() {
    static UserAlertHandler instance;
    return instance;
}
PluginLoadFailureHandler& pluginLoadFailureHandler() {
    static PluginLoadFailureHandler instance;
    return instance;
}
}  // namespace

void setUserAlertHandler(UserAlertHandler newHandler) {
    handler() = std::move(newHandler);
}

void notifyUserAlert(const juce::String& message) {
    if (auto& h = handler())
        h(message);
}

void setPluginLoadFailureHandler(PluginLoadFailureHandler newHandler) {
    pluginLoadFailureHandler() = std::move(newHandler);
}

void notifyPluginLoadFailure(const juce::String& plugin, const juce::String& reason) {
    if (auto& h = pluginLoadFailureHandler())
        h(plugin, reason);
}

}  // namespace magda
