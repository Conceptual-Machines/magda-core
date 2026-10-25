#include "SettingsLink.hpp"

namespace magda {

namespace {
SettingsOpener& opener() {
    static SettingsOpener instance;
    return instance;
}
}  // namespace

void openSettings(SettingsSectionId section, const juce::String& tab) {
    if (opener())
        opener()(section, tab);
}

void setSettingsOpener(SettingsOpener newOpener) {
    opener() = std::move(newOpener);
}

}  // namespace magda
