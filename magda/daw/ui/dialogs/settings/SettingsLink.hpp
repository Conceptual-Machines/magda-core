#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace magda {

/** @brief The sidebar entries of the Settings window, in sidebar order (#3162). */
enum class SettingsSectionId {
    General,
    Appearance,
    AudioMidi,
    Controllers,
    Connections,
    Plugins,
    AI,
    Rendering,
    Paths,
    Defaults,
    Shortcuts,
};

using SettingsOpener = std::function<void(SettingsSectionId, const juce::String& tab)>;

/** @brief Open the Settings window on @p section; @p tab names a tab or sub-page. */
void openSettings(SettingsSectionId section, const juce::String& tab = {});

/** @brief Installed by the main window, so callers need not link the window itself. */
void setSettingsOpener(SettingsOpener opener);

}  // namespace magda
