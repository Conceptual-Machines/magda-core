#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace magda {

using UserAlertHandler = std::function<void(const juce::String&)>;

/**
 * Registers the handler that turns notifyUserAlert() into a visible
 * notification. Set once by the UI layer at startup; left unset in headless
 * targets (magda_cli, magda-mcp), where notifyUserAlert() is then a no-op.
 * Keeps core/engine code (which those headless targets also link) from
 * depending directly on a UI component.
 */
void setUserAlertHandler(UserAlertHandler handler);

/** Best-effort user notification for a failure with no other route to the
 *  UI (e.g. an exception caught during teardown). No-op with no handler. */
void notifyUserAlert(const juce::String& message);

using PluginLoadFailureHandler =
    std::function<void(const juce::String& plugin, const juce::String& reason)>;

/// Registered by the UI, which warns with a dialog; unset in headless targets (#3150).
void setPluginLoadFailureHandler(PluginLoadFailureHandler handler);

/// A plugin that could not be instantiated, with the host's or the plugin's reason.
void notifyPluginLoadFailure(const juce::String& plugin, const juce::String& reason);

}  // namespace magda
