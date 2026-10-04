#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

namespace detail {
struct AppShortcutTargets {
    juce::ApplicationCommandManager* manager = nullptr;
    juce::ApplicationCommandTarget* target = nullptr;
};

inline AppShortcutTargets& appShortcutTargets() {
    static AppShortcutTargets targets;
    return targets;
}
}  // namespace detail

/// The app's shortcuts and the target they run on, for top-level windows outside the main
/// window's key chain. The main window sets them while it lives.
inline void setAppShortcuts(juce::ApplicationCommandManager* manager,
                            juce::ApplicationCommandTarget* target) {
    detail::appShortcutTargets() = {manager, target};
}

/**
 * @brief Runs the app shortcut mapped to @p key, if any. For a floating window's keyPressed.
 *
 * Invoked on the main target directly: the manager would look for a target from the active window,
 * and with a floating window in front it finds none.
 */
inline bool invokeAppShortcut(const juce::KeyPress& key, juce::Component* origin) {
    const auto& [manager, target] = detail::appShortcutTargets();
    if (manager == nullptr || target == nullptr)
        return false;
    const auto command = manager->getKeyMappings()->findCommandForKeyPress(key);
    if (command == 0)
        return false;
    juce::ApplicationCommandTarget::InvocationInfo info(command);
    info.invocationMethod = juce::ApplicationCommandTarget::InvocationInfo::fromKeyPress;
    info.keyPress = key;
    info.isKeyDown = true;
    info.originatingComponent = origin;
    return target->invoke(info, false);
}

}  // namespace magda
