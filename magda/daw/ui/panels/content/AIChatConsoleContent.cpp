#include "AIChatConsoleContent.hpp"

#include "../../../../agents/daw_agent.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

// ============================================================================
// RequestThread
// ============================================================================

AIChatConsoleContent::RequestThread::RequestThread(AIChatConsoleContent& owner)
    : juce::Thread("AI Chat Request"), owner_(owner) {}

void AIChatConsoleContent::RequestThread::run() {
    // Step 1: Generate DSL on background thread (HTTP call)
    auto dslResult = owner_.agent_->generateDSL(owner_.pendingMessage_.toStdString());

    if (threadShouldExit())
        return;

    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(&owner_);

    // Step 2: Execute DSL on message thread (modifies tracks/clips)
    juce::MessageManager::callAsync([safeThis, dslResult = std::move(dslResult)]() {
        if (!safeThis)
            return;

        std::string response;
        if (dslResult.hasError) {
            response = dslResult.error;
        } else {
            response = safeThis->agent_->executeDSL(dslResult);
        }

        safeThis->stopTimer();

        // Remove "Thinking..." line and show response
        auto currentText = safeThis->chatHistory_.getText();
        auto thinkingPos = currentText.lastIndexOf("AI: Thinking");
        if (thinkingPos >= 0) {
            auto lineEnd = currentText.indexOf(thinkingPos, "\n");
            if (lineEnd < 0)
                lineEnd = currentText.length();
            currentText =
                currentText.substring(0, thinkingPos) + currentText.substring(lineEnd + 1);
        }

        // Format response - prefix errors distinctly
        juce::String formattedResponse(response);
        if (formattedResponse.startsWith("Error:") ||
            formattedResponse.startsWith("DSL execution error:")) {
            formattedResponse = "[!] " + formattedResponse;
        }

        safeThis->chatHistory_.setText(currentText + "AI: " + formattedResponse + "\n\n");
        safeThis->chatHistory_.moveCaretToEnd();
        safeThis->inputBox_.setEnabled(true);
        safeThis->inputBox_.grabKeyboardFocus();
        safeThis->processing_ = false;
    });
}

// ============================================================================
// AIChatConsoleContent
// ============================================================================

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
    stopTimer();

    // Signal cancellation
    shouldStop_ = true;
    if (agent_)
        agent_->requestCancel();

    // Stop the background thread with a timeout
    if (requestThread_) {
        requestThread_->signalThreadShouldExit();
        requestThread_->stopThread(5000);
        requestThread_.reset();
    }

    if (agent_)
        agent_->stop();
}

void AIChatConsoleContent::sendMessage(const juce::String& text) {
    // If a previous request thread is still around, wait briefly
    if (requestThread_ && requestThread_->isThreadRunning()) {
        agent_->requestCancel();
        requestThread_->signalThreadShouldExit();
        requestThread_->stopThread(2000);
    }

    processing_ = true;
    inputBox_.clear();
    inputBox_.setEnabled(false);

    appendToChat("You: " + text);
    appendToChat("AI: Thinking");

    // Reset cancel state and start new request
    shouldStop_ = false;
    agent_->resetCancel();
    pendingMessage_ = text;

    dotCount_ = 0;
    startTimer(400);  // Animate dots every 400ms

    requestThread_ = std::make_unique<RequestThread>(*this);
    requestThread_->startThread();
}

void AIChatConsoleContent::timerCallback() {
    if (!processing_) {
        stopTimer();
        return;
    }

    dotCount_ = (dotCount_ % 3) + 1;
    juce::String dots;
    for (int i = 0; i < dotCount_; ++i)
        dots += ".";

    // Update the "Thinking" line in the chat history
    auto currentText = chatHistory_.getText();
    auto thinkingPos = currentText.lastIndexOf("AI: Thinking");
    if (thinkingPos >= 0) {
        auto lineEnd = currentText.indexOf(thinkingPos, "\n");
        if (lineEnd < 0)
            lineEnd = currentText.length();
        chatHistory_.setText(currentText.substring(0, thinkingPos) + "AI: Thinking" + dots +
                             currentText.substring(lineEnd));
        chatHistory_.moveCaretToEnd();
    }
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
