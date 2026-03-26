#include "ScriptingConsoleContent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

ScriptingConsoleContent::ScriptingConsoleContent() {
    setName("Scripting Console");

    placeholderLabel_.setText("DSL console has moved to the AI Chat panel (DSL tab).",
                              juce::dontSendNotification);
    placeholderLabel_.setFont(FontManager::getInstance().getUIFont(13.0f));
    placeholderLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    placeholderLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(placeholderLabel_);
}

void ScriptingConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void ScriptingConsoleContent::resized() {
    placeholderLabel_.setBounds(getLocalBounds());
}

void ScriptingConsoleContent::onActivated() {}
void ScriptingConsoleContent::onDeactivated() {}

}  // namespace magda::daw::ui
