#include "AIChatConsoleContent.hpp"

#include <algorithm>

#include "../../../../agents/daw_agent.hpp"
#include "../../../../agents/dsl_interpreter.hpp"
#include "../../../core/ClipManager.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "BinaryData.h"
#include "PluginBrowserContent.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda::daw::ui {

// ============================================================================
// AutocompletePopup
// ============================================================================

class AIChatConsoleContent::AutocompletePopup : public juce::Component, public juce::ListBoxModel {
  public:
    AutocompletePopup(AIChatConsoleContent& owner) : owner_(owner) {
        listBox_.setModel(this);
        listBox_.setRowHeight(22);
        listBox_.setColour(juce::ListBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
        listBox_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
        addAndMakeVisible(listBox_);
    }

    enum class Mode { Alias, SlashCommand };

    void updateFilter(const juce::String& filter) {
        mode_ = Mode::Alias;
        filter_ = filter.toLowerCase();
        filtered_.clear();
        filteredCommands_.clear();

        for (const auto& entry : owner_.allAliases_) {
            if (filter_.isEmpty() || entry.alias.toLowerCase().contains(filter_) ||
                entry.pluginName.toLowerCase().contains(filter_)) {
                filtered_.push_back(&entry);
            }
        }

        listBox_.updateContent();
        if (!filtered_.empty())
            listBox_.selectRow(0);

        int rows = juce::jmin(static_cast<int>(filtered_.size()), 8);
        setSize(getWidth(), rows * 22 + 2);
    }

    void updateSlashFilter(const juce::String& filter) {
        mode_ = Mode::SlashCommand;
        filter_ = filter.toLowerCase();
        filtered_.clear();
        filteredCommands_.clear();

        for (const auto& cmd : owner_.slashCommands_) {
            if (filter_.isEmpty() || cmd.name.toLowerCase().startsWith(filter_))
                filteredCommands_.push_back(&cmd);
        }

        listBox_.updateContent();
        if (!filteredCommands_.empty())
            listBox_.selectRow(0);

        int rows = juce::jmin(static_cast<int>(filteredCommands_.size()), 8);
        setSize(getWidth(), rows * 22 + 2);
    }

    bool isEmpty() const {
        return mode_ == Mode::Alias ? filtered_.empty() : filteredCommands_.empty();
    }

    Mode getMode() const {
        return mode_;
    }

    const AliasEntry* getSelectedEntry() const {
        int row = listBox_.getSelectedRow();
        if (mode_ == Mode::Alias && row >= 0 && row < static_cast<int>(filtered_.size()))
            return filtered_[static_cast<size_t>(row)];
        return nullptr;
    }

    const SlashCommand* getSelectedCommand() const {
        int row = listBox_.getSelectedRow();
        if (mode_ == Mode::SlashCommand && row >= 0 &&
            row < static_cast<int>(filteredCommands_.size()))
            return filteredCommands_[static_cast<size_t>(row)];
        return nullptr;
    }

    void selectNext() {
        int current = listBox_.getSelectedRow();
        if (current < getNumRows() - 1)
            listBox_.selectRow(current + 1);
    }

    void selectPrevious() {
        int current = listBox_.getSelectedRow();
        if (current > 0)
            listBox_.selectRow(current - 1);
    }

    // ListBoxModel
    int getNumRows() override {
        return mode_ == Mode::Alias ? static_cast<int>(filtered_.size())
                                    : static_cast<int>(filteredCommands_.size());
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override {
        if (rowNumber < 0 || rowNumber >= getNumRows())
            return;

        if (rowIsSelected) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
            g.fillRect(0, 0, width, height);
        }

        if (mode_ == Mode::Alias) {
            const auto& entry = *filtered_[static_cast<size_t>(rowNumber)];
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("@" + entry.alias, 6, 0, width / 2, height,
                       juce::Justification::centredLeft);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(entry.pluginName, width / 2, 0, width / 2 - 6, height,
                       juce::Justification::centredRight);
        } else {
            const auto& cmd = *filteredCommands_[static_cast<size_t>(rowNumber)];
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getMonoFont(11.0f));
            g.drawText("/" + cmd.name, 6, 0, width / 3, height, juce::Justification::centredLeft);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(10.0f));
            g.drawText(cmd.description, width / 3, 0, width * 2 / 3 - 6, height,
                       juce::Justification::centredLeft);
        }
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (mode_ == Mode::Alias && row >= 0 && row < static_cast<int>(filtered_.size())) {
            owner_.insertAlias(filtered_[static_cast<size_t>(row)]->alias);
        } else if (mode_ == Mode::SlashCommand && row >= 0 &&
                   row < static_cast<int>(filteredCommands_.size())) {
            owner_.insertSlashCommand(filteredCommands_[static_cast<size_t>(row)]->name);
        }
    }

    void resized() override {
        listBox_.setBounds(getLocalBounds());
    }

  private:
    AIChatConsoleContent& owner_;
    juce::ListBox listBox_;
    juce::String filter_;
    Mode mode_ = Mode::Alias;
    std::vector<const AliasEntry*> filtered_;
    std::vector<const SlashCommand*> filteredCommands_;
};

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
        // If autocomplete is showing, insert the selected item instead of sending
        if (autocompletePopup_ && autocompletePopup_->isVisible()) {
            if (autocompletePopup_->getMode() == AutocompletePopup::Mode::SlashCommand) {
                if (auto* cmd = autocompletePopup_->getSelectedCommand()) {
                    insertSlashCommand(cmd->name);
                    return;
                }
            } else {
                if (auto* entry = autocompletePopup_->getSelectedEntry()) {
                    insertAlias(entry->alias);
                    return;
                }
            }
        }
        auto text = inputBox_.getText().trim();
        if (text.isNotEmpty() && !processing_)
            sendMessage(text);
    };
    inputBox_.onTextChange = [this]() {
        auto text = inputBox_.getText();
        int caretPos = inputBox_.getCaretPosition();

        // Check for / at start of input (slash commands)
        if (text.startsWith("/")) {
            // Extract the command token (up to first space or end)
            int spacePos = text.indexOf(" ");
            if (spacePos < 0 || caretPos <= spacePos) {
                // Still typing the command name
                auto filter = text.substring(1, caretPos);
                showSlashAutocomplete(filter);
                return;
            }
        }

        // Find the @ token before the caret
        int atPos = -1;
        for (int i = caretPos - 1; i >= 0; --i) {
            auto ch = text[i];
            if (ch == '@') {
                atPos = i;
                break;
            }
            if (ch == ' ' || ch == '\n')
                break;
        }

        if (atPos >= 0) {
            auto filter = text.substring(atPos + 1, caretPos);
            showAutocomplete(filter);
        } else {
            hideAutocomplete();
        }
    };
    inputBox_.onEscapeKey = [this]() {
        if (autocompletePopup_ && autocompletePopup_->isVisible()) {
            hideAutocomplete();
        }
    };
    addAndMakeVisible(inputBox_);

    // Register key listener on input box for autocomplete navigation
    inputBox_.addKeyListener(this);

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
    sendButton_.setAlpha(0.35f);
    sendButton_.onClick = [this]() {
        auto text = inputBox_.getText().trim();
        if (text.isNotEmpty() && !processing_)
            sendMessage(text);
    };
    addAndMakeVisible(sendButton_);

    // Clear chat button
    auto deleteSvg =
        juce::Drawable::createFromImageData(BinaryData::delete_svg, BinaryData::delete_svgSize);
    clearButton_.setImages(deleteSvg.get());
    clearButton_.setEdgeIndent(4);
    clearButton_.setColour(juce::DrawableButton::backgroundColourId,
                           juce::Colours::transparentBlack);
    clearButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                           juce::Colours::transparentBlack);
    clearButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    clearButton_.setTooltip("Clear chat");
    clearButton_.setAlpha(0.35f);
    clearButton_.onClick = [this]() {
        chatHistory_.setText(juce::String::charToString(0x25C6) + " MAGDA\n\n");
    };
    addAndMakeVisible(clearButton_);

    // Copy chat button
    auto copySvg = juce::Drawable::createFromImageData(BinaryData::copycontent_svg,
                                                       BinaryData::copycontent_svgSize);
    copyButton_.setImages(copySvg.get());
    copyButton_.setEdgeIndent(4);
    copyButton_.setColour(juce::DrawableButton::backgroundColourId,
                          juce::Colours::transparentBlack);
    copyButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                          juce::Colours::transparentBlack);
    copyButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    copyButton_.setTooltip("Copy chat to clipboard");
    copyButton_.setAlpha(0.35f);
    copyButton_.onClick = [this]() {
        juce::SystemClipboard::copyTextToClipboard(chatHistory_.getText());
    };
    addAndMakeVisible(copyButton_);

    // Tab buttons (MAGDA flat tab style)
    setupTabButtons();

    // DSL output area
    dslOutput_.setMultiLine(true);
    dslOutput_.setReadOnly(true);
    dslOutput_.setFont(FontManager::getInstance().getMonoFont(12.0f));
    dslOutput_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    dslOutput_.setColour(juce::TextEditor::textColourId, juce::Colour(0xff88ff88));
    dslOutput_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    dslOutput_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    dslOutput_.setText("MAGDA DSL Console\nCtrl+Enter to execute.\n\n");

    // DSL code editor
    dslEditor_ = std::make_unique<juce::CodeEditorComponent>(dslDocument_, &dslTokeniser_);
    dslEditor_->setFont(FontManager::getInstance().getMonoFont(13.0f));
    dslEditor_->setColour(juce::CodeEditorComponent::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    dslEditor_->setColour(juce::CodeEditorComponent::lineNumberBackgroundId,
                          juce::Colour(0xff252526));
    dslEditor_->setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour(0xff858585));
    dslEditor_->setColour(juce::CaretComponent::caretColourId, juce::Colour(0xff88ff88));
    dslEditor_->setColour(juce::CodeEditorComponent::highlightColourId, juce::Colour(0xff264f78));
    dslEditor_->setLineNumbersShown(true);
    dslEditor_->setTabSize(2, true);
    dslEditor_->setScrollbarThickness(8);
    dslEditor_->addKeyListener(this);

    // DSL status bar
    dslStatusLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    dslStatusLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff007acc));
    dslStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
#if JUCE_MAC
    dslStatusLabel_.setText("  MAGDA DSL  |  Cmd+Enter: Run  |  Cmd+L: Clear",
                            juce::dontSendNotification);
#else
    dslStatusLabel_.setText("  MAGDA DSL  |  Ctrl+Enter: Run  |  Ctrl+L: Clear",
                            juce::dontSendNotification);
#endif

    // DSL components start hidden
    dslOutput_.setVisible(false);
    dslEditor_->setVisible(false);
    dslStatusLabel_.setVisible(false);
    addChildComponent(dslOutput_);
    addChildComponent(*dslEditor_);
    addChildComponent(dslStatusLabel_);

    // Register for selection changes
    magda::SelectionManager::getInstance().addListener(this);

    // Register for project lifecycle events
    magda::ProjectManager::getInstance().addListener(this);

    // Create and start the DAW agent
    agent_ = std::make_unique<magda::DAWAgent>();
    agent_->start();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    if (dslEditor_)
        dslEditor_->removeKeyListener(this);
    inputBox_.removeKeyListener(this);
    autocompletePopup_.reset();
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

juce::String AIChatConsoleContent::resolveAliases(const juce::String& text) {
    if (allAliases_.empty())
        buildAliasList();

    // Sort by alias length descending to avoid prefix collisions
    // (e.g. @pro matching inside @pro_q_3)
    auto sorted = allAliases_;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.alias.length() > b.alias.length(); });

    auto resolved = text;
    for (const auto& entry : sorted) {
        auto token = "@" + entry.alias;
        if (resolved.contains(token))
            resolved = resolved.replace(token, entry.pluginName);
    }
    return resolved;
}

juce::String AIChatConsoleContent::rewriteSlashCommand(const juce::String& text) {
    auto trimmed = text.trimStart();

    // /groove <request> — constrain LLM to groove/swing template operations only
    if (trimmed.startsWithIgnoreCase("/groove ")) {
        auto request = trimmed.substring(8).trim();
        return "[COMMAND: GROOVE] The user wants to create or apply a SWING/GROOVE TEMPLATE "
               "(timing feel that shifts note playback timing). "
               "You MUST use ONLY groove.new(), groove.set(), groove.extract(), or groove.list() "
               "commands. Do NOT create tracks, clips, or notes. "
               "User request: " +
               request;
    }

    return text;
}

void AIChatConsoleContent::sendMessage(const juce::String& text) {
    // Direct DSL execution — bypass AI agent entirely
    if (text.trimStart().startsWith("/dsl ")) {
        auto dslCode = text.trimStart().substring(5).trim();
        appendToChat(juce::String::charToString(0x25CF) + " " + text);

        magda::dsl::Interpreter interpreter;
        bool success = interpreter.execute(dslCode.toRawUTF8());

        if (success) {
            auto results = interpreter.getResults();
            if (results.isEmpty())
                results = "OK";
            appendToChat(juce::String::charToString(0x25C6) + " " + results);
        } else {
            appendToChat(juce::String::charToString(0x25C6) +
                         " Error: " + juce::String(interpreter.getError()));
        }

        inputBox_.clear();
        return;
    }

    // If a previous request thread is still around, stop it before starting a new one
    if (requestThread_ && requestThread_->isThreadRunning()) {
        agent_->requestCancel();
        requestThread_->signalThreadShouldExit();
        if (!requestThread_->stopThread(2000))
            DBG("AIChatConsole: Warning - previous request thread did not stop within timeout");
        requestThread_.reset();
    }

    // Resolve @alias mentions to real plugin names before sending to the LLM
    auto resolvedText = resolveAliases(text);

    // Slash-command prefixes: rewrite user message with LLM context hints
    resolvedText = rewriteSlashCommand(resolvedText);

    processing_ = true;
    inputBox_.clear();
    inputBox_.setEnabled(false);
    sendButton_.setEnabled(false);

    appendToChat(juce::String::charToString(0x25CF) + " " + text);
    appendToChat(juce::String::charToString(0x25C6) + " Thinking");

    // Reset cancel state and start new request
    shouldStop_ = false;
    agent_->resetCancel();

    pendingMessage_ = resolvedText;

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

    // Tab buttons at bottom
    auto tabBar = bounds.removeFromBottom(22);
    aiTabButton_.setBounds(tabBar.removeFromLeft(40));
    dslTabButton_.setBounds(tabBar.removeFromLeft(40));
    bounds.removeFromBottom(4);  // Spacing above tabs

    if (activeTab_ == ConsoleTab::AI) {
        // Context bar above tabs
        auto bottomBar = bounds.removeFromBottom(26);
        bottomBarBounds_ = bottomBar;
        sendButton_.setBounds(bottomBar.removeFromRight(22));
        contextIconBounds_ = bottomBar.removeFromLeft(22);
        contextLabel_.setBounds(bottomBar);

        // Input box directly above context bar (no gap — unified shape)
        auto inputArea = bounds.removeFromBottom(80);
        inputBox_.setBounds(inputArea);

        bounds.removeFromBottom(8);  // Spacing
        chatHistory_.setBounds(bounds);

        // Clear + copy buttons inside chat panel, top-right corner
        auto chatBounds = chatHistory_.getBounds();
        int btnSize = 20;
        int margin = 4;
        copyButton_.setBounds(chatBounds.getRight() - btnSize - margin, chatBounds.getY() + margin,
                              btnSize, btnSize);
        clearButton_.setBounds(chatBounds.getRight() - 2 * btnSize - margin - 2,
                               chatBounds.getY() + margin, btnSize, btnSize);
        clearButton_.toFront(false);
        copyButton_.toFront(false);
    } else {
        // DSL tab layout
        dslStatusLabel_.setBounds(bounds.removeFromBottom(20));
        auto editorHeight = juce::jmax(60, bounds.getHeight() / 3);
        dslEditor_->setBounds(bounds.removeFromBottom(editorHeight));
        bounds.removeFromBottom(1);  // Separator
        dslOutput_.setBounds(bounds);
    }
}

void AIChatConsoleContent::onActivated() {
    buildAliasList();
    if (isShowing()) {
        if (activeTab_ == ConsoleTab::AI)
            inputBox_.grabKeyboardFocus();
        else if (dslEditor_)
            dslEditor_->grabKeyboardFocus();
    }
}

void AIChatConsoleContent::onDeactivated() {
    // Could save chat history here
}

// ============================================================================
// Tab Switching
// ============================================================================

void AIChatConsoleContent::setupTabButtons() {
    auto setupTab = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        btn.setColour(juce::TextButton::buttonOnColourId,
                      DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f));
        btn.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(9001);
        btn.setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        addAndMakeVisible(btn);
    };

    setupTab(aiTabButton_);
    setupTab(dslTabButton_);

    aiTabButton_.setToggleState(true, juce::dontSendNotification);

    aiTabButton_.onClick = [this]() { switchTab(ConsoleTab::AI); };
    dslTabButton_.onClick = [this]() { switchTab(ConsoleTab::DSL); };
}

void AIChatConsoleContent::switchTab(ConsoleTab tab) {
    if (activeTab_ == tab)
        return;
    activeTab_ = tab;

    bool isAI = (tab == ConsoleTab::AI);

    // AI components
    chatHistory_.setVisible(isAI);
    inputBox_.setVisible(isAI);
    sendButton_.setVisible(isAI);
    contextLabel_.setVisible(isAI);
    clearButton_.setVisible(isAI);
    copyButton_.setVisible(isAI);

    // DSL components
    dslOutput_.setVisible(!isAI);
    dslEditor_->setVisible(!isAI);
    dslStatusLabel_.setVisible(!isAI);

    resized();
    repaint();

    if (isAI)
        inputBox_.grabKeyboardFocus();
    else
        dslEditor_->grabKeyboardFocus();
}

void AIChatConsoleContent::executeDSL() {
    auto code = dslDocument_.getAllContent().trim();
    if (code.isEmpty())
        return;

    // History
    if (dslHistory_.isEmpty() || dslHistory_.strings.getLast() != code)
        dslHistory_.add(code);
    dslHistoryIndex_ = -1;

    // Echo
    appendDSLOutput("> " + code + "\n", juce::Colour(0xff88ff88));

    // Built-in commands
    if (code == "help") {
        appendDSLOutput("MAGDA DSL Commands:\n"
                        "  track(name=\"X\")              - Reference/create track\n"
                        "  track(id=1)                  - Reference track by index\n"
                        "  .clip.new(bar=1, length_bars=4) - Create MIDI clip\n"
                        "  .fx.add(name=\"reverb\")       - Add effect\n"
                        "  .notes.add(pitch=C4, beat=0) - Add note\n"
                        "  .notes.add_chord(root=C4, quality=major)\n"
                        "  filter(tracks, ...).delete()  - Bulk operations\n\n",
                        juce::Colour(0xff569cd6));
        dslDocument_.replaceAllContent({});
        return;
    }
    if (code == "clear") {
        dslOutput_.clear();
        dslOutput_.setText("Output cleared.\n\n");
        dslDocument_.replaceAllContent({});
        return;
    }

    // Execute
    magda::dsl::Interpreter interpreter;
    bool success = interpreter.execute(code.toRawUTF8());

    if (success) {
        auto results = interpreter.getResults();
        if (results.isEmpty())
            results = "OK";
        appendDSLOutput(results + "\n\n", juce::Colour(0xffd4d4d4));
    } else {
        appendDSLOutput("Error: " + juce::String(interpreter.getError()) + "\n\n",
                        juce::Colour(0xfff48771));
    }

    dslDocument_.replaceAllContent({});
}

void AIChatConsoleContent::appendDSLOutput(const juce::String& text, juce::Colour colour) {
    dslOutput_.setColour(juce::TextEditor::textColourId, colour);
    dslOutput_.moveCaretToEnd();
    dslOutput_.insertTextAtCaret(text);
    dslOutput_.moveCaretToEnd();
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

// ============================================================================
// Plugin alias autocomplete
// ============================================================================

void AIChatConsoleContent::buildAliasList() {
    allAliases_.clear();

    // Internal plugins
    auto addInternal = [this](const juce::String& name) {
        allAliases_.push_back({PluginBrowserInfo::generateAlias(name), name});
    };
    addInternal("Test Tone");
    addInternal("4OSC Synth");
    addInternal("Equaliser");
    addInternal("Compressor");
    addInternal("Reverb");
    addInternal("Delay");
    addInternal("Chorus");
    addInternal("Phaser");
    addInternal("Filter");
    addInternal("Pitch Shift");
    addInternal("IR Reverb");
    addInternal("Utility");
    addInternal(juce::String(audio::MagdaSamplerPlugin::getPluginName()));
    addInternal(juce::String(audio::DrumGridPlugin::getPluginName()));

    // External plugins from KnownPluginList
    if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
            magda::TrackManager::getInstance().getAudioEngine())) {
        auto& knownPlugins = engine->getKnownPluginList();
        auto types = knownPlugins.getTypes();
        DBG("AIChatConsole: buildAliasList - KnownPluginList has " << types.size() << " plugins");
        for (const auto& desc : types) {
            auto alias = PluginBrowserInfo::generateAlias(desc.name);
            allAliases_.push_back({alias, desc.name});
        }
    } else {
        DBG("AIChatConsole: buildAliasList - engine not available via TrackManager");
    }

    // Load custom alias overrides
    auto aliasFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("MAGDA")
                         .getChildFile("plugin_aliases.xml");

    if (aliasFile.existsAsFile()) {
        if (auto xml = juce::parseXML(aliasFile)) {
            for (auto* elem : xml->getChildIterator()) {
                auto key = elem->getStringAttribute("key");
                auto alias = elem->getStringAttribute("alias");
                // Find and update the matching entry
                for (auto& entry : allAliases_) {
                    // Match by uniqueId or by plugin name
                    if (entry.pluginName == key ||
                        PluginBrowserInfo::generateAlias(entry.pluginName) ==
                            PluginBrowserInfo::generateAlias(key)) {
                        entry.alias = alias;
                        break;
                    }
                }
            }
        }
    }

    // Sort by alias
    std::sort(allAliases_.begin(), allAliases_.end(),
              [](const AliasEntry& a, const AliasEntry& b) { return a.alias < b.alias; });
}

void AIChatConsoleContent::showAutocomplete(const juce::String& filter) {
    if (allAliases_.empty())
        buildAliasList();

    DBG("AIChatConsole: showAutocomplete filter=\"" << filter
                                                    << "\", total aliases=" << allAliases_.size());

    if (!autocompletePopup_) {
        autocompletePopup_ = std::make_unique<AutocompletePopup>(*this);
        addAndMakeVisible(*autocompletePopup_);
    }

    auto inputBounds = inputBox_.getBounds();
    int popupWidth = inputBounds.getWidth();
    autocompletePopup_->setSize(popupWidth, 8 * 22 + 2);  // Initial size, updateFilter adjusts
    autocompletePopup_->updateFilter(filter);

    if (autocompletePopup_->isEmpty()) {
        hideAutocomplete();
        return;
    }

    // Position above the input box
    int popupHeight = autocompletePopup_->getHeight();
    autocompletePopup_->setBounds(inputBounds.getX(), inputBounds.getY() - popupHeight, popupWidth,
                                  popupHeight);
    autocompletePopup_->setVisible(true);
    autocompletePopup_->toFront(false);
}

void AIChatConsoleContent::hideAutocomplete() {
    if (autocompletePopup_)
        autocompletePopup_->setVisible(false);
}

void AIChatConsoleContent::insertAlias(const juce::String& alias) {
    auto text = inputBox_.getText();
    int caretPos = inputBox_.getCaretPosition();

    // Find the @ that started this completion
    int atPos = -1;
    for (int i = caretPos - 1; i >= 0; --i) {
        auto ch = text[i];
        if (ch == '@') {
            atPos = i;
            break;
        }
        if (ch == ' ' || ch == '\n')
            break;
    }

    if (atPos >= 0) {
        // Replace @partial with @full_alias
        auto before = text.substring(0, atPos);
        auto after = text.substring(caretPos);
        auto newText = before + "@" + alias + " " + after;
        inputBox_.setText(newText, false);
        inputBox_.setCaretPosition(atPos + 1 + alias.length() + 1);
    }

    hideAutocomplete();
    inputBox_.grabKeyboardFocus();
}

bool AIChatConsoleContent::keyPressed(const juce::KeyPress& key, juce::Component*) {
    // DSL tab key handling
    if (activeTab_ == ConsoleTab::DSL) {
        // Ctrl+Enter — execute DSL
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == juce::KeyPress::returnKey) {
            executeDSL();
            return true;
        }
        // Ctrl+L — clear DSL output
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'L') {
            dslOutput_.clear();
            dslOutput_.setText("Output cleared.\n\n");
            return true;
        }
        return false;
    }

    // AI tab — autocomplete navigation
    if (!autocompletePopup_ || !autocompletePopup_->isVisible())
        return false;

    if (key == juce::KeyPress::upKey) {
        autocompletePopup_->selectPrevious();
        return true;
    }
    if (key == juce::KeyPress::downKey) {
        autocompletePopup_->selectNext();
        return true;
    }
    if (key == juce::KeyPress::tabKey) {
        if (autocompletePopup_->getMode() == AutocompletePopup::Mode::SlashCommand) {
            if (auto* cmd = autocompletePopup_->getSelectedCommand()) {
                insertSlashCommand(cmd->name);
                return true;
            }
        } else {
            if (auto* entry = autocompletePopup_->getSelectedEntry()) {
                insertAlias(entry->alias);
                return true;
            }
        }
    }
    if (key == juce::KeyPress::escapeKey) {
        hideAutocomplete();
        return true;
    }

    return false;
}

void AIChatConsoleContent::buildSlashCommands() {
    slashCommands_ = {
        {"groove", "Create or apply swing/groove timing templates"},
    };
}

void AIChatConsoleContent::showSlashAutocomplete(const juce::String& filter) {
    if (slashCommands_.empty())
        buildSlashCommands();

    if (!autocompletePopup_) {
        autocompletePopup_ = std::make_unique<AutocompletePopup>(*this);
        addAndMakeVisible(*autocompletePopup_);
    }

    auto inputBounds = inputBox_.getBounds();
    int popupWidth = inputBounds.getWidth();
    autocompletePopup_->setSize(popupWidth, 8 * 22 + 2);
    autocompletePopup_->updateSlashFilter(filter);

    if (autocompletePopup_->isEmpty()) {
        hideAutocomplete();
        return;
    }

    int popupHeight = autocompletePopup_->getHeight();
    autocompletePopup_->setBounds(inputBounds.getX(), inputBounds.getY() - popupHeight, popupWidth,
                                  popupHeight);
    autocompletePopup_->setVisible(true);
    autocompletePopup_->toFront(false);
}

void AIChatConsoleContent::insertSlashCommand(const juce::String& command) {
    auto text = inputBox_.getText();
    // Find the end of the /command token
    int spacePos = text.indexOf(" ");
    auto after = (spacePos >= 0) ? text.substring(spacePos) : "";
    auto newText = "/" + command + " " + after.trimStart();
    inputBox_.setText(newText, false);
    inputBox_.setCaretPosition(newText.length());

    hideAutocomplete();
    inputBox_.grabKeyboardFocus();
}

}  // namespace magda::daw::ui
