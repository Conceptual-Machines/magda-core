#include "AIChatConsoleContent.hpp"

#include "../../../../agents/daw_agent.hpp"
#include "../../../core/ClipManager.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"

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
        auto thinkingPos = currentText.lastIndexOf("\xe2\x97\x86 Thinking");
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

        safeThis->chatHistory_.setText(currentText + "\xe2\x97\x86 " + formattedResponse + "\n\n");
        safeThis->chatHistory_.moveCaretToEnd();
        safeThis->inputBox_.setEnabled(true);
        safeThis->sendButton_.setEnabled(true);
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
    auto monoFont = FontManager::getInstance().getMonoFont(13.0f);
    chatHistory_.setMultiLine(true);
    chatHistory_.setReadOnly(true);
    chatHistory_.setFont(monoFont);
    chatHistory_.setColour(juce::TextEditor::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    chatHistory_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    chatHistory_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    chatHistory_.setColour(juce::TextEditor::focusedOutlineColourId, DarkTheme::getBorderColour());
    chatHistory_.setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    chatHistory_.setText(
        "Welcome! Ask me anything about your project...\n"
        "Try: \"create a bass track\" or \"create a drums track and mute it\"\n\n");
    addAndMakeVisible(chatHistory_);

    // Input box
    inputBox_.setFont(monoFont);
    inputBox_.setMultiLine(true);
    inputBox_.setReturnKeyStartsNewLine(false);
    inputBox_.setTextToShowWhenEmpty("Type a message...", DarkTheme::getSecondaryTextColour());
    inputBox_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    inputBox_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    inputBox_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    inputBox_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    inputBox_.onReturnKey = [this]() {
        auto text = inputBox_.getText().trim();
        if (text.isNotEmpty() && !processing_)
            sendMessage(text);
    };
    addAndMakeVisible(inputBox_);

    // Context label (always visible, inside bottom bar)
    contextLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    contextLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    contextLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    contextLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    contextLabel_.setBorderSize(juce::BorderSize<int>(0, 8, 0, 4));
    contextLabel_.setInterceptsMouseClicks(true, false);
    contextLabel_.addMouseListener(this, false);
    addAndMakeVisible(contextLabel_);

    // Send button (embedded in bottom bar) — SVG icon
    auto enterSvg =
        juce::Drawable::createFromImageData(BinaryData::enter_svg, BinaryData::enter_svgSize);
    sendButton_.setImages(enterSvg.get());
    sendButton_.setColour(juce::DrawableButton::backgroundColourId,
                          juce::Colours::transparentBlack);
    sendButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                          juce::Colours::transparentBlack);
    sendButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    sendButton_.onClick = [this]() {
        auto text = inputBox_.getText().trim();
        if (text.isNotEmpty() && !processing_)
            sendMessage(text);
    };
    addAndMakeVisible(sendButton_);

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
    sendButton_.setEnabled(false);

    appendToChat("\xe2\x97\x8f " + text);
    appendToChat("\xe2\x97\x86 Thinking");

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
    auto thinkingPos = currentText.lastIndexOf("\xe2\x97\x86 Thinking");
    if (thinkingPos >= 0) {
        auto lineEnd = currentText.indexOf(thinkingPos, "\n");
        if (lineEnd < 0)
            lineEnd = currentText.length();
        chatHistory_.setText(currentText.substring(0, thinkingPos) + "\xe2\x97\x86 Thinking" +
                             dots + currentText.substring(lineEnd));
        chatHistory_.moveCaretToEnd();
    }
}

void AIChatConsoleContent::appendToChat(const juce::String& text) {
    chatHistory_.moveCaretToEnd();
    chatHistory_.insertTextAtCaret(text + "\n");
}

void AIChatConsoleContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    // Draw input box + bottom bar as one unified rounded rectangle
    auto inputBounds = inputBox_.getBounds();
    auto barBounds = bottomBarBounds_;
    auto combined = inputBounds.getUnion(barBounds).toFloat();

    g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    g.fillRoundedRectangle(combined, 4.0f);
    g.setColour(DarkTheme::getBorderColour());
    g.drawRoundedRectangle(combined, 4.0f, 1.0f);

    // Thin horizontal border between input and bottom bar
    float separatorY = static_cast<float>(inputBounds.getBottom());
    g.drawHorizontalLine(static_cast<int>(separatorY), combined.getX() + 1.0f,
                         combined.getRight() - 1.0f);
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing

    // Bottom bar (always visible): context label left, send button right
    auto bottomBar = bounds.removeFromBottom(26);
    bottomBarBounds_ = bottomBar;
    sendButton_.setBounds(bottomBar.removeFromRight(26));
    contextLabel_.setBounds(bottomBar);

    // Input box directly above bottom bar (no gap — unified shape)
    auto inputArea = bounds.removeFromBottom(60);
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
    contextLabel_.setText(contextText_, juce::dontSendNotification);
    contextLabel_.setColour(juce::Label::textColourId,
                            contextEnabled_ ? DarkTheme::getAccentColour()
                                            : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
    repaint();
}

void AIChatConsoleContent::mouseUp(const juce::MouseEvent& event) {
    if (event.originalComponent == &contextLabel_) {
        contextEnabled_ = !contextEnabled_;
        updateContextBar();
    }
}

}  // namespace magda::daw::ui
