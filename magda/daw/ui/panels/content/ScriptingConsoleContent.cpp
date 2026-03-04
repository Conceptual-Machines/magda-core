#include "ScriptingConsoleContent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

ScriptingConsoleContent::ScriptingConsoleContent() {
    setName("Scripting Console");

    // Setup title
    titleLabel_.setText("Script Console", juce::dontSendNotification);
    titleLabel_.setFont(FontManager::getInstance().getUIFont(14.0f));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    // Output area (monospace font)
    outputArea_.setMultiLine(true);
    outputArea_.setReadOnly(true);
    outputArea_.setFont(FontManager::getInstance().getMonoFont(12.0f));
    outputArea_.setColour(juce::TextEditor::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    outputArea_.setColour(juce::TextEditor::textColourId,
                          juce::Colour(0xFF88FF88));  // Green console text
    outputArea_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    outputArea_.setText("MAGDA Script Console v0.1\nType 'help' for available commands.\n\n");
    addAndMakeVisible(outputArea_);

    // Input box (monospace font)
    inputBox_.setFont(FontManager::getInstance().getMonoFont(12.0f));
    inputBox_.setTextToShowWhenEmpty("> Enter command...", DarkTheme::getSecondaryTextColour());
    inputBox_.setColour(juce::TextEditor::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::BACKGROUND));
    inputBox_.setColour(juce::TextEditor::textColourId,
                        juce::Colour(0xFF88FF88));  // Green console text
    inputBox_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    inputBox_.onReturnKey = [this]() {
        auto command = inputBox_.getText();
        if (command.isNotEmpty()) {
            executeCommand(command);
            inputBox_.clear();
        }
    };
    addAndMakeVisible(inputBox_);
}

void ScriptingConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void ScriptingConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing

    inputBox_.setBounds(bounds.removeFromBottom(24));
    bounds.removeFromBottom(8);  // Spacing

    outputArea_.setBounds(bounds);
}

void ScriptingConsoleContent::executeCommand(const juce::String& command) {
    outputArea_.moveCaretToEnd();
    outputArea_.insertTextAtCaret("> " + command + "\n");

    // Simple command handling
    if (command == "help") {
        outputArea_.insertTextAtCaret("Available commands:\n"
                                      "  help    - Show this help\n"
                                      "  clear   - Clear console\n"
                                      "  version - Show version info\n"
                                      "\n");
    } else if (command == "clear") {
        outputArea_.clear();
        outputArea_.setText("MAGDA Script Console v0.1\n\n");
    } else if (command == "version") {
        outputArea_.insertTextAtCaret("MAGDA v0.1.0\n\n");
    } else {
        outputArea_.insertTextAtCaret("Unknown command: " + command + "\n\n");
    }
}

void ScriptingConsoleContent::onActivated() {
    if (isShowing())
        inputBox_.grabKeyboardFocus();
}

void ScriptingConsoleContent::onDeactivated() {
    // Could save history here
}

}  // namespace magda::daw::ui
