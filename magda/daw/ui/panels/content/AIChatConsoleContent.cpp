#include "AIChatConsoleContent.hpp"

#include "../../../../agents/daw_agent.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

AIChatConsoleContent::AIChatConsoleContent() {
    setName("AI Chat");

    // Setup title
    titleLabel_.setText("AI Assistant", juce::dontSendNotification);
    titleLabel_.setFont(FontManager::getInstance().getUIFont(14.0f));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    // Chat history area
    chatHistory_.setMultiLine(true);
    chatHistory_.setReadOnly(true);
    chatHistory_.setColour(juce::TextEditor::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    chatHistory_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    chatHistory_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    chatHistory_.setText(
        "Welcome! Ask me anything about your project...\n"
        "Try: \"create a bass track\" or \"create a drums track and mute it\"\n\n");
    addAndMakeVisible(chatHistory_);

    // Input box
    inputBox_.setTextToShowWhenEmpty("Type a message...", DarkTheme::getSecondaryTextColour());
    inputBox_.setColour(juce::TextEditor::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    inputBox_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    inputBox_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    inputBox_.onReturnKey = [this]() {
        auto text = inputBox_.getText().trim();
        if (text.isNotEmpty() && !processing_)
            sendMessage(text);
    };
    addAndMakeVisible(inputBox_);

    // Create and start the DAW agent
    agent_ = std::make_unique<magda::DAWAgent>();
    agent_->start();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    if (agent_)
        agent_->stop();
}

void AIChatConsoleContent::sendMessage(const juce::String& text) {
    processing_ = true;
    inputBox_.clear();
    inputBox_.setEnabled(false);

    appendToChat("You: " + text);
    appendToChat("AI: Thinking...");

    // Run agent on background thread
    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(this);
    auto messageText = text.toStdString();

    juce::Thread::launch([safeThis, messageText]() {
        if (!safeThis)
            return;

        auto response = safeThis->agent_->processMessage(messageText);

        juce::MessageManager::callAsync([safeThis, response]() {
            if (!safeThis)
                return;

            // Remove "Thinking..." and show response
            auto currentText = safeThis->chatHistory_.getText();
            auto thinkingPos = currentText.lastIndexOf("AI: Thinking...");
            if (thinkingPos >= 0) {
                safeThis->chatHistory_.setText(currentText.substring(0, thinkingPos) +
                                               "AI: " + juce::String(response) + "\n\n");
            } else {
                safeThis->appendToChat("AI: " + juce::String(response));
            }

            safeThis->chatHistory_.moveCaretToEnd();
            safeThis->inputBox_.setEnabled(true);
            safeThis->inputBox_.grabKeyboardFocus();
            safeThis->processing_ = false;
        });
    });
}

void AIChatConsoleContent::appendToChat(const juce::String& text) {
    chatHistory_.moveCaretToEnd();
    chatHistory_.insertTextAtCaret(text + "\n");
}

void AIChatConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing

    inputBox_.setBounds(bounds.removeFromBottom(28));
    bounds.removeFromBottom(8);  // Spacing

    chatHistory_.setBounds(bounds);
}

void AIChatConsoleContent::onActivated() {
    inputBox_.grabKeyboardFocus();
}

void AIChatConsoleContent::onDeactivated() {
    // Could save chat history here
}

}  // namespace magda::daw::ui
