#include "ScriptingConsoleContent.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

ScriptingConsoleContent::ScriptingConsoleContent() {
    setName("Scripting Console");

    placeholderLabel_.setText("Run DSL from the AI console: type /dsl followed by the code.",
                              juce::dontSendNotification);
    placeholderLabel_.setFont(FontManager::getInstance().getUIFont(13.0f));
    placeholderLabel_.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    placeholderLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(placeholderLabel_);
}

void ScriptingConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getPanelBackgroundColour());
}

void ScriptingConsoleContent::resized() {
    placeholderLabel_.setBounds(getLocalBounds());
}

void ScriptingConsoleContent::onActivated() {}
void ScriptingConsoleContent::onDeactivated() {}

}  // namespace magda::daw::ui
