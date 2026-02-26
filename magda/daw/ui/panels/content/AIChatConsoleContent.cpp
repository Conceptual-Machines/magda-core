#include "AIChatConsoleContent.hpp"

#include "../../../../agents/daw_agent.hpp"
#include "../../../core/ClipManager.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackManager.hpp"
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
    inputBox_.setMultiLine(true);
    inputBox_.setReturnKeyStartsNewLine(false);
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

    // Context bar (initially hidden) — styled to match input box
    contextLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    contextLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    contextLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    contextLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    contextLabel_.setBorderSize(juce::BorderSize<int>(0, 4, 0, 4));
    contextLabel_.setInterceptsMouseClicks(true, false);
    contextLabel_.addMouseListener(this, false);
    contextLabel_.setVisible(false);
    addAndMakeVisible(contextLabel_);

    // Register for selection changes
    magda::SelectionManager::getInstance().addListener(this);

    // Create and start the DAW agent
    agent_ = std::make_unique<magda::DAWAgent>();
    agent_->start();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    magda::SelectionManager::getInstance().removeListener(this);
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

    // Prepend selection context if available and enabled
    if (contextEnabled_ && contextText_.isNotEmpty())
        pendingMessage_ = "[Context: " + contextText_ + "] " + text;
    else
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

    // Draw context bar background, border, and separator
    if (contextLabel_.isVisible()) {
        auto ctxBounds = contextLabel_.getBounds();
        // Fill background to match input box
        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.fillRect(ctxBounds);
        // Border
        g.setColour(DarkTheme::getBorderColour());
        g.drawRect(ctxBounds.toFloat(), 1.0f);
    }
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing

    bool showContextRow = contextLabel_.isVisible();

    // Input area at the bottom: input box + context bar below
    int inputHeight = 60 + (showContextRow ? 20 : 0);
    auto inputArea = bounds.removeFromBottom(inputHeight);

    if (showContextRow) {
        auto contextRow = inputArea.removeFromBottom(20);
        contextLabel_.setBounds(contextRow);
    }

    inputBox_.setBounds(inputArea);

    bounds.removeFromBottom(8);  // Spacing
    chatHistory_.setBounds(bounds);
}

void AIChatConsoleContent::onActivated() {
    inputBox_.grabKeyboardFocus();
}

void AIChatConsoleContent::onDeactivated() {
    // Could save chat history here
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void AIChatConsoleContent::selectionTypeChanged(magda::SelectionType newType) {
    if (newType == magda::SelectionType::None) {
        contextText_.clear();
        updateContextBar();
    }
}

void AIChatConsoleContent::trackSelectionChanged(magda::TrackId trackId) {
    auto* track = magda::TrackManager::getInstance().getTrack(trackId);
    if (track != nullptr)
        contextText_ = "Track \"" + track->name + "\"";
    else
        contextText_ = "Track " + juce::String(trackId);
    updateContextBar();
}

void AIChatConsoleContent::clipSelectionChanged(magda::ClipId clipId) {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip != nullptr) {
        auto* track = magda::TrackManager::getInstance().getTrack(clip->trackId);
        juce::String trackPart = track != nullptr ? "Track \"" + track->name + "\""
                                                  : "Track " + juce::String(clip->trackId);
        contextText_ = trackPart + " > Clip \"" + clip->name + "\"";
    } else {
        contextText_ = "Clip " + juce::String(clipId);
    }
    updateContextBar();
}

void AIChatConsoleContent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    auto* track = magda::TrackManager::getInstance().getTrack(path.trackId);
    juce::String trackPart =
        track != nullptr ? "Track \"" + track->name + "\"" : "Track " + juce::String(path.trackId);

    auto deviceId = path.getDeviceId();
    if (deviceId != magda::INVALID_DEVICE_ID) {
        auto* device = magda::TrackManager::getInstance().getDevice(path.trackId, deviceId);
        if (device != nullptr)
            contextText_ = trackPart + " > " + device->name;
        else
            contextText_ = trackPart + " > Device " + juce::String(deviceId);
    } else {
        contextText_ = trackPart;
    }
    updateContextBar();
}

void AIChatConsoleContent::updateContextBar() {
    bool visible = contextText_.isNotEmpty();
    contextLabel_.setText(contextText_, juce::dontSendNotification);
    contextLabel_.setVisible(visible);
    contextLabel_.setColour(juce::Label::textColourId,
                            contextEnabled_ ? DarkTheme::getAccentColour()
                                            : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
    resized();
    repaint();
}

void AIChatConsoleContent::mouseUp(const juce::MouseEvent& event) {
    if (event.originalComponent == &contextLabel_) {
        contextEnabled_ = !contextEnabled_;
        updateContextBar();
    }
}

}  // namespace magda::daw::ui
