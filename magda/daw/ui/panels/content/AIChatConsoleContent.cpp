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
        auto thinkingPos =
            currentText.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
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

        safeThis->chatHistory_.setText(currentText + juce::String::charToString(0x25C6) + " " +
                                       formattedResponse + "\n\n");
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

    // Chat history area
    auto monoFont = FontManager::getInstance().getMonoFont(13.0f);
    chatHistory_.setMultiLine(true);
    chatHistory_.setReadOnly(true);
    chatHistory_.setFont(monoFont);
    chatHistory_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    chatHistory_.setColour(juce::TextEditor::textColourId, DarkTheme::getSecondaryTextColour());
    chatHistory_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    chatHistory_.setColour(juce::TextEditor::focusedOutlineColourId,
                           juce::Colours::transparentBlack);
    chatHistory_.setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    chatHistory_.setText(juce::String::charToString(0x25C6) + " MAGDA\n\n");
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

    // Load context icons
    trackIconDrawable_ =
        juce::Drawable::createFromImageData(BinaryData::track_svg, BinaryData::track_svgSize);
    clipIconDrawable_ =
        juce::Drawable::createFromImageData(BinaryData::clip_svg, BinaryData::clip_svgSize);

    // Context label (always visible, inside bottom bar)
    contextLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    contextLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    contextLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    contextLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    contextLabel_.setBorderSize(juce::BorderSize<int>(0, 2, 0, 4));
    contextLabel_.setInterceptsMouseClicks(true, false);
    contextLabel_.addMouseListener(this, false);
    addAndMakeVisible(contextLabel_);

    // Send button (embedded in bottom bar) — SVG icon
    auto enterSvg =
        juce::Drawable::createFromImageData(BinaryData::enter_svg, BinaryData::enter_svgSize);
    sendButton_.setImages(enterSvg.get());
    sendButton_.setEdgeIndent(5);
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

    // Register for project lifecycle events
    magda::ProjectManager::getInstance().addListener(this);

    // Create and start the DAW agent
    agent_ = std::make_unique<magda::DAWAgent>();
    agent_->start();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    magda::ProjectManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
    stopTimer();

    // Signal cancellation
    shouldStop_ = true;
    if (agent_)
        agent_->requestCancel();

    // Stop the background thread with a timeout
    if (requestThread_) {
        requestThread_->signalThreadShouldExit();
        if (!requestThread_->stopThread(5000))
            DBG("AIChatConsole: Warning - request thread did not stop within timeout");
        requestThread_.reset();
    }

    if (agent_)
        agent_->stop();
}

void AIChatConsoleContent::sendMessage(const juce::String& text) {
    // If a previous request thread is still around, stop it before starting a new one
    if (requestThread_ && requestThread_->isThreadRunning()) {
        agent_->requestCancel();
        requestThread_->signalThreadShouldExit();
        if (!requestThread_->stopThread(2000))
            DBG("AIChatConsole: Warning - previous request thread did not stop within timeout");
        requestThread_.reset();
    }

    processing_ = true;
    inputBox_.clear();
    inputBox_.setEnabled(false);
    sendButton_.setEnabled(false);

    appendToChat(juce::String::charToString(0x25CF) + " " + text);
    appendToChat(juce::String::charToString(0x25C6) + " Thinking");

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
    auto thinkingPos = currentText.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
    if (thinkingPos >= 0) {
        auto lineEnd = currentText.indexOf(thinkingPos, "\n");
        if (lineEnd < 0)
            lineEnd = currentText.length();
        chatHistory_.setText(currentText.substring(0, thinkingPos) +
                             juce::String::charToString(0x25C6) + " Thinking" + dots +
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

    // Draw chat history background with rounded corners
    auto chatBounds = chatHistory_.getBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    g.fillRoundedRectangle(chatBounds, 4.0f);
    g.setColour(DarkTheme::getBorderColour());
    g.drawRoundedRectangle(chatBounds, 4.0f, 1.0f);

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

    // Draw context icon
    if (contextIcon_ != ContextIcon::None) {
        juce::Drawable* icon = nullptr;
        if (contextIcon_ == ContextIcon::Track || contextIcon_ == ContextIcon::Device)
            icon = trackIconDrawable_.get();
        else if (contextIcon_ == ContextIcon::Clip)
            icon = clipIconDrawable_.get();

        if (icon) {
            auto iconBounds = contextIconBounds_.toFloat().reduced(6.0f);
            auto colour = contextEnabled_ ? DarkTheme::getAccentColour()
                                          : DarkTheme::getSecondaryTextColour().withAlpha(0.3f);
            // Tint a copy to avoid mutating the stored drawable
            static const auto svgGrey = juce::Colour(0xFFB3B3B3);
            static const auto svgWhite = juce::Colours::white;
            auto iconCopy = icon->createCopy();
            iconCopy->replaceColour(svgGrey, colour);
            iconCopy->replaceColour(svgWhite, colour);
            iconCopy->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        }
    }
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Bottom bar (always visible): icon + context label left, send button right
    auto bottomBar = bounds.removeFromBottom(26);
    bottomBarBounds_ = bottomBar;
    sendButton_.setBounds(bottomBar.removeFromRight(22));
    contextIconBounds_ = bottomBar.removeFromLeft(22);
    contextLabel_.setBounds(bottomBar);

    // Input box directly above bottom bar (no gap — unified shape)
    auto inputArea = bounds.removeFromBottom(60);
    inputBox_.setBounds(inputArea);

    bounds.removeFromBottom(8);  // Spacing
    chatHistory_.setBounds(bounds);
}

void AIChatConsoleContent::onActivated() {
    if (isShowing())
        inputBox_.grabKeyboardFocus();
}

void AIChatConsoleContent::onDeactivated() {
    // Could save chat history here
}

// ============================================================================
// ProjectManagerListener
// ============================================================================

void AIChatConsoleContent::projectOpened(const magda::ProjectInfo& /*info*/) {
    // Reset chat history
    chatHistory_.setText(juce::String::charToString(0x25C6) + " MAGDA\n\n");

    // Cancel any in-flight request
    if (requestThread_ && requestThread_->isThreadRunning()) {
        agent_->requestCancel();
        requestThread_->signalThreadShouldExit();
        requestThread_->stopThread(2000);
        requestThread_.reset();
    }
    stopTimer();
    processing_ = false;
    inputBox_.setEnabled(true);
    sendButton_.setEnabled(true);
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void AIChatConsoleContent::selectionTypeChanged(magda::SelectionType newType) {
    if (newType == magda::SelectionType::None) {
        contextText_.clear();
        contextIcon_ = ContextIcon::None;
        updateContextBar();
    }
}

void AIChatConsoleContent::trackSelectionChanged(magda::TrackId trackId) {
    auto* track = magda::TrackManager::getInstance().getTrack(trackId);
    contextText_ = track != nullptr ? track->name : juce::String(trackId);
    contextIcon_ = ContextIcon::Track;
    updateContextBar();
}

void AIChatConsoleContent::clipSelectionChanged(magda::ClipId clipId) {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip != nullptr) {
        auto* track = magda::TrackManager::getInstance().getTrack(clip->trackId);
        juce::String trackName = track != nullptr ? track->name : juce::String(clip->trackId);
        contextText_ = trackName + " > " + clip->name;
    } else {
        contextText_ = juce::String(clipId);
    }
    contextIcon_ = ContextIcon::Clip;
    updateContextBar();
}

void AIChatConsoleContent::chainNodeSelectionChanged(const magda::ChainNodePath& path) {
    auto* track = magda::TrackManager::getInstance().getTrack(path.trackId);
    juce::String trackName = track != nullptr ? track->name : juce::String(path.trackId);

    auto deviceId = path.getDeviceId();
    if (deviceId != magda::INVALID_DEVICE_ID) {
        auto* device = magda::TrackManager::getInstance().getDevice(path.trackId, deviceId);
        if (device != nullptr)
            contextText_ = trackName + " > " + device->name;
        else
            contextText_ = trackName + " > " + juce::String(deviceId);
    } else {
        contextText_ = trackName;
    }
    contextIcon_ = ContextIcon::Device;
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
    if (event.originalComponent == &contextLabel_ ||
        (event.originalComponent == this && contextIconBounds_.contains(event.getPosition()))) {
        contextEnabled_ = !contextEnabled_;
        updateContextBar();
    }
}

}  // namespace magda::daw::ui
