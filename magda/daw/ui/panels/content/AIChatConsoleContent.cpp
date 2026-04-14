#include "AIChatConsoleContent.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "../../../../agents/command_agent.hpp"
#include "../../../../agents/compact_executor.hpp"
#include "../../../../agents/daw_agent.hpp"
#include "../../../../agents/dsl_interpreter.hpp"
#include "../../../../agents/llama_model_manager.hpp"
#include "../../../../agents/llm_presets.hpp"
#include "../../../../agents/music_agent.hpp"
#include "../../../../agents/router_agent.hpp"
#include "../../../core/ClipManager.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../components/common/SvgButton.hpp"
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
    auto message = owner_.pendingMessage_.toStdString();
    auto safeThis = juce::Component::SafePointer<AIChatConsoleContent>(&owner_);

    auto totalStart = std::chrono::steady_clock::now();
    double routerMs = 0.0, agentMs = 0.0;

    // Step 1: Classify intent via router
    std::string intent = "COMMAND";  // default fallback
    if (owner_.routerAgent_) {
        auto routerStart = std::chrono::steady_clock::now();
        auto classification = owner_.routerAgent_->classify(message);
        routerMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                             routerStart)
                       .count();
        if (!classification.hasError) {
            intent = classification.intent;
            DBG("MAGDA Router: intent=" + juce::String(intent) + " (" + juce::String(routerMs, 0) +
                "ms)");
        } else {
            DBG("MAGDA Router: error: " + juce::String(classification.error) +
                ", defaulting to COMMAND");
        }
    }

    if (threadShouldExit())
        return;

    // streamAnchor marks the text position where streamed output begins,
    // so we can replace it with execution results later. Shared ownership so
    // callAsync lambdas keep it alive even if this thread exits before they
    // run (capturing by reference to a stack local would be UB).
    auto streamAnchor = std::make_shared<std::atomic<int>>(-1);

    // Helper: replace "Thinking..." with streaming output area
    auto startStreaming = [safeThis, streamAnchor]() {
        juce::MessageManager::callAsync([safeThis, streamAnchor]() {
            if (!safeThis)
                return;
            safeThis->stopTimer();
            auto text = safeThis->chatHistory_.getText();
            auto thinkingPos = text.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
            if (thinkingPos >= 0) {
                auto lineEnd = text.indexOf(thinkingPos, "\n");
                if (lineEnd < 0)
                    lineEnd = text.length();
                text = text.substring(0, thinkingPos) + text.substring(lineEnd + 1);
            }
            streamAnchor->store(text.length());
            safeThis->chatHistory_.setText(text);
            safeThis->chatHistory_.moveCaretToEnd();
        });
    };

    // Coalesced token buffer for single-intent streaming. Every token would
    // otherwise post its own callAsync; on a fast stream that buries the
    // message queue and the final execute-callAsync sits behind hundreds of
    // stale text appends. We buffer tokens and keep at most one flush
    // callback in flight at a time.
    struct SingleStream {
        std::mutex mu;
        juce::String pending;
        std::atomic<bool> flushPending{false};
    };
    auto singleState = std::make_shared<SingleStream>();

    auto appendToken = [safeThis, singleState](const juce::String& token) {
        {
            std::lock_guard<std::mutex> lk(singleState->mu);
            singleState->pending += token;
        }
        bool expected = false;
        if (!singleState->flushPending.compare_exchange_strong(expected, true))
            return;
        juce::MessageManager::callAsync([safeThis, singleState]() {
            singleState->flushPending.store(false);
            if (!safeThis)
                return;
            juce::String chunk;
            {
                std::lock_guard<std::mutex> lk(singleState->mu);
                chunk = std::move(singleState->pending);
                singleState->pending.clear();
            }
            if (chunk.isEmpty())
                return;
            auto text = safeThis->chatHistory_.getText();
            safeThis->chatHistory_.setText(text + chunk);
            safeThis->chatHistory_.moveCaretToEnd();
        });
    };

    // Token callback for streaming — starts stream on first token
    bool streamStarted = false;
    auto onToken = [&](const juce::String& token) -> bool {
        if (threadShouldExit())
            return false;
        if (!streamStarted) {
            startStreaming();
            streamStarted = true;
            wait(16);
        }
        appendToken(token);
        return true;
    };

    // Step 2: Dispatch to agents based on classification
    std::string dslCode;                                // DSL from command agent
    std::vector<magda::Instruction> musicInstructions;  // IR from music agent
    std::string musicDescription;                       // description from DSL music agent
    std::string error;

    auto agentStart = std::chrono::steady_clock::now();

    if (intent == "BOTH") {
        // Run both agents in parallel, each streaming into its own labeled section.
        // A shared render callback rebuilds the streaming region from both buffers so
        // tokens from one agent never interleave into the other's text.
        startStreaming();  // clear "◆ Thinking" and lock anchor

        struct DualStream {
            std::mutex mu;
            std::string cmdBuf;
            std::string musicBuf;
            std::atomic<bool> renderPending{false};
        };
        auto state = std::make_shared<DualStream>();

        // Coalesce render posts: if one is already queued, skip — the queued
        // callback will read the latest buffer contents when it runs. This
        // prevents the message thread from being buried under hundreds of
        // stale rebuilds while streaming, which caused a visible stall
        // between stream-end and execute-callAsync.
        auto render = [safeThis, state, streamAnchor]() {
            bool expected = false;
            if (!state->renderPending.compare_exchange_strong(expected, true))
                return;
            juce::MessageManager::callAsync([safeThis, state, streamAnchor]() {
                state->renderPending.store(false);
                if (!safeThis)
                    return;
                int anchor = streamAnchor->load();
                auto full = safeThis->chatHistory_.getText();
                if (anchor < 0 || anchor > full.length())
                    return;
                juce::String cmd, music;
                {
                    std::lock_guard<std::mutex> lk(state->mu);
                    cmd = juce::String(state->cmdBuf);
                    music = juce::String(state->musicBuf);
                }
                juce::String section;
                section << "[command]\n" << cmd << "\n\n[music]\n" << music;
                safeThis->chatHistory_.setText(full.substring(0, anchor) + section);
                safeThis->chatHistory_.moveCaretToEnd();
            });
        };

        auto cmdOnToken = [this, state, render](const juce::String& t) -> bool {
            if (threadShouldExit())
                return false;
            {
                std::lock_guard<std::mutex> lk(state->mu);
                state->cmdBuf += t.toStdString();
            }
            render();
            return true;
        };
        auto musicOnToken = [this, state, render](const juce::String& t) -> bool {
            if (threadShouldExit())
                return false;
            {
                std::lock_guard<std::mutex> lk(state->mu);
                state->musicBuf += t.toStdString();
            }
            render();
            return true;
        };

        std::future<magda::CommandAgent::GenerateResult> commandFuture;
        std::future<magda::MusicAgent::GenerateResult> musicFuture;

        if (owner_.commandAgent_) {
            commandFuture = std::async(std::launch::async, [this, &message, cmdOnToken]() {
                return owner_.commandAgent_->generateStreaming(message, cmdOnToken);
            });
        }
        if (owner_.musicAgent_) {
            musicFuture = std::async(std::launch::async, [this, &message, musicOnToken]() {
                return owner_.musicAgent_->generateStreaming(message, musicOnToken);
            });
        }

        if (commandFuture.valid()) {
            auto result = commandFuture.get();
            if (result.hasError) {
                error = result.error;
            } else {
                dslCode = result.dslOutput;
            }
        }
        if (musicFuture.valid()) {
            auto result = musicFuture.get();
            if (result.hasError) {
                if (error.empty())
                    error = result.error;
                else
                    error += "\n" + result.error;
            } else {
                musicInstructions = std::move(result.instructions);
                musicDescription = std::move(result.description);
            }
        }
        if (threadShouldExit())
            return;
    } else if (intent == "COMMAND") {
        if (owner_.commandAgent_) {
            auto result = owner_.commandAgent_->generateStreaming(message, onToken);
            if (threadShouldExit())
                return;
            if (result.hasError)
                error = result.error;
            else
                dslCode = result.dslOutput;
        }
    } else if (intent == "MUSIC") {
        if (owner_.musicAgent_) {
            auto result = owner_.musicAgent_->generateStreaming(message, onToken);
            if (threadShouldExit())
                return;
            if (result.hasError) {
                error = result.error;
            } else {
                musicInstructions = std::move(result.instructions);
                musicDescription = std::move(result.description);
            }
        }
    }

    agentMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - agentStart)
            .count();
    auto totalMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - totalStart)
            .count();

    DBG("MAGDA Timing: router=" + juce::String(routerMs, 0) +
        "ms, agent=" + juce::String(agentMs, 0) + "ms, total=" + juce::String(totalMs, 0) + "ms");

    if (threadShouldExit())
        return;

    int anchor = streamAnchor->load();

    // Step 3: Execute on message thread, replacing streamed output
    juce::MessageManager::callAsync(
        [safeThis, dsl = std::move(dslCode), musicIR = std::move(musicInstructions),
         musicDesc = std::move(musicDescription), error = std::move(error), anchor, routerMs,
         agentMs, totalMs]() {
            if (!safeThis)
                return;

            std::string response;
            bool hasContent = !dsl.empty() || !musicIR.empty();

            if (!error.empty() && !hasContent) {
                response = error;
            } else {
                // Coalesce per-clip property notifications emitted during bulk
                // note insertion. Without this each AddMidiNoteCommand fires
                // listeners that fully rewrite the TE MIDI sequence and repaint
                // the piano-roll, producing O(n^2) work and a visible stall
                // between stream-end and notes appearing on screen.
                magda::ClipManager::BatchScope batchScope;

                // Execute DSL from command agent
                int commandClipId = -1;
                if (!dsl.empty()) {
                    magda::dsl::Interpreter interpreter;
                    if (interpreter.execute(dsl.c_str())) {
                        auto results = interpreter.getResults().toStdString();
                        response = results.empty() ? "OK" : results;
                        commandClipId = interpreter.getCurrentClipId();
                    } else {
                        response = "Error: " + std::string(interpreter.getError());
                    }
                }

                // Execute IR from music agent
                if (!musicIR.empty()) {
                    if (!musicDesc.empty()) {
                        if (!response.empty())
                            response += "\n";
                        response += musicDesc;
                    }
                    magda::CompactExecutor executor;
                    // Hand the command agent's freshly-created clip (if any)
                    // explicitly to the music executor. Otherwise it will
                    // auto-create a new clip — we never want it to silently
                    // fill whatever clip the user happened to have selected.
                    executor.setSeedClipId(commandClipId);
                    if (executor.execute(musicIR)) {
                        // Name the clip after the music agent's description so
                        // users can see what each generated clip represents.
                        // Safe to rename unconditionally: the executor no
                        // longer inherits user-selected clips, so every clip
                        // it touches is either command-seeded or auto-created
                        // in this turn (both have default names).
                        // Truncate to keep track-lane labels readable.
                        if (!musicDesc.empty() && executor.getCurrentClipId() >= 0) {
                            constexpr int kMaxClipNameLen = 40;
                            juce::String clipName(musicDesc);
                            // Cut at the first sentence/clause boundary if one
                            // falls before the hard limit.
                            auto clausePos = clipName.indexOfAnyOf(".,;");
                            if (clausePos > 0 && clausePos < kMaxClipNameLen)
                                clipName = clipName.substring(0, clausePos);
                            if (clipName.length() > kMaxClipNameLen)
                                clipName = clipName.substring(0, kMaxClipNameLen).trim() + "…";
                            magda::ClipManager::getInstance().setClipName(
                                executor.getCurrentClipId(), clipName.trim());
                        }
                        auto results = executor.getResults().toStdString();
                        if (!response.empty())
                            response += "\n";
                        response += results.empty() ? "OK" : results;
                    } else {
                        if (!response.empty())
                            response += "\n";
                        response += "Error: " + executor.getError().toStdString();
                    }
                }

                if (!error.empty())
                    response += "\n[Warning] " + error;
            }

            // Append timing info
            auto formatMs = [](double ms) -> std::string {
                if (ms >= 1000.0)
                    return juce::String(ms / 1000.0, 1).toStdString() + "s";
                return juce::String(ms, 0).toStdString() + "ms";
            };
            response += "\n[" + formatMs(routerMs) + " router, " + formatMs(agentMs) + " agent, " +
                        formatMs(totalMs) + " total]";

            safeThis->stopTimer();

            auto currentText = safeThis->chatHistory_.getText();

            // Replace streamed raw output with execution results
            if (anchor >= 0 && anchor <= currentText.length()) {
                currentText = currentText.substring(0, anchor);
            } else {
                // Streaming never started — remove "Thinking..." if present
                auto thinkingPos =
                    currentText.lastIndexOf(juce::String::charToString(0x25C6) + " Thinking");
                if (thinkingPos >= 0) {
                    auto lineEnd = currentText.indexOf(thinkingPos, "\n");
                    if (lineEnd < 0)
                        lineEnd = currentText.length();
                    currentText =
                        currentText.substring(0, thinkingPos) + currentText.substring(lineEnd + 1);
                }
            }

            juce::String formattedResponse(response);
            if (formattedResponse.startsWith("Error:") ||
                formattedResponse.startsWith("DSL execution error:")) {
                formattedResponse = "[!] " + formattedResponse;
            }

            currentText += juce::String::charToString(0x25C6) + " " + formattedResponse + "\n\n";
            safeThis->chatHistory_.setText(currentText);
            safeThis->chatHistory_.moveCaretToEnd();
            safeThis->inputBox_.setEnabled(true);
            safeThis->processing_ = false;
            safeThis->restoreSendIcon();
            safeThis->inputBox_.grabKeyboardFocus();
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
        if (processing_) {
            cancelRequest();
            return;
        }
        auto text = inputBox_.getText().trim();
        if (text.isNotEmpty())
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

    // Config status bar
    configStatusLabel_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    configStatusLabel_.setColour(juce::Label::textColourId,
                                 DarkTheme::getSecondaryTextColour().withAlpha(0.6f));
    configStatusLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    configStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(configStatusLabel_);

    // Model load/unload button (shown only for local_embedded preset)
    serverToggleButton_ = std::make_unique<magda::SvgButton>(
        "ModelToggle", BinaryData::server_play_svg, BinaryData::server_play_svgSize);
    serverToggleButton_->onClick = [this]() {
        auto& mgr = magda::LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            mgr.unloadModel();
            updateConfigStatus();
        } else {
            auto& config = magda::Config::getInstance();
            auto modelPath = config.getLocalModelPath();
            if (modelPath.empty()) {
                configStatusLabel_.setText("No model path configured", juce::dontSendNotification);
                configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
                return;
            }
            magda::LlamaModelManager::Config cfg;
            cfg.modelPath = modelPath;
            cfg.gpuLayers = config.getLocalLlamaGpuLayers();
            cfg.contextSize = config.getLocalLlamaContextSize();
            configStatusLabel_.setText("Loading model...", juce::dontSendNotification);
            configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::yellow);
            std::thread([this, cfg]() {
                bool ok = magda::LlamaModelManager::getInstance().loadModel(cfg);
                juce::MessageManager::callAsync([this, ok]() {
                    if (!ok)
                        DBG("Console: failed to load local model");
                    updateConfigStatus();
                });
            }).detach();
        }
    };
    addChildComponent(*serverToggleButton_);  // hidden by default

    updateConfigStatus();

    // Register for selection changes
    magda::SelectionManager::getInstance().addListener(this);

    // Register for project lifecycle events
    magda::ProjectManager::getInstance().addListener(this);

    // Register for config changes (e.g. preset changed in settings dialog)
    magda::Config::getInstance().addListener(this);

    // Create agents
    agent_ = std::make_unique<magda::DAWAgent>();  // legacy DSL REPL
    agent_->start();
    routerAgent_ = std::make_unique<magda::RouterAgent>();
    commandAgent_ = std::make_unique<magda::CommandAgent>();
    musicAgent_ = std::make_unique<magda::MusicAgent>();
}

AIChatConsoleContent::~AIChatConsoleContent() {
    if (dslEditor_)
        dslEditor_->removeKeyListener(this);
    inputBox_.removeKeyListener(this);
    autocompletePopup_.reset();
    magda::Config::getInstance().removeListener(this);
    magda::ProjectManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
    stopTimer();

    // Signal cancellation
    shouldStop_ = true;
    if (agent_)
        agent_->requestCancel();
    if (routerAgent_)
        routerAgent_->requestCancel();
    if (commandAgent_)
        commandAgent_->requestCancel();
    if (musicAgent_)
        musicAgent_->requestCancel();

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

    // Convert @alias to <alias> token format for the LLM — resolved at DSL execution time
    auto resolved = text;
    for (const auto& entry : sorted) {
        auto token = "@" + entry.alias;
        if (resolved.contains(token))
            resolved = resolved.replace(token, "<" + entry.alias + ">");
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
        if (agent_)
            agent_->requestCancel();
        if (routerAgent_)
            routerAgent_->requestCancel();
        if (commandAgent_)
            commandAgent_->requestCancel();
        if (musicAgent_)
            musicAgent_->requestCancel();
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

    // Swap send button to stop icon
    auto stopSvg =
        juce::Drawable::createFromImageData(BinaryData::stop_off_svg, BinaryData::stop_off_svgSize);
    sendButton_.setImages(stopSvg.get());
    sendButton_.setAlpha(0.6f);

    appendToChat(juce::String::charToString(0x25CF) + " " + text);
    appendToChat(juce::String::charToString(0x25C6) + " Thinking");

    // Reset cancel state and start new request
    shouldStop_ = false;
    if (agent_)
        agent_->resetCancel();
    if (routerAgent_)
        routerAgent_->resetCancel();
    if (commandAgent_)
        commandAgent_->resetCancel();
    if (musicAgent_)
        musicAgent_->resetCancel();

    pendingMessage_ = resolvedText;

    dotCount_ = 0;
    startTimer(400);  // Animate dots every 400ms

    requestThread_ = std::make_unique<RequestThread>(*this);
    requestThread_->startThread();
}

void AIChatConsoleContent::cancelRequest() {
    if (!processing_)
        return;

    shouldStop_ = true;
    if (agent_)
        agent_->requestCancel();
    if (routerAgent_)
        routerAgent_->requestCancel();
    if (commandAgent_)
        commandAgent_->requestCancel();
    if (musicAgent_)
        musicAgent_->requestCancel();

    if (requestThread_ && requestThread_->isThreadRunning()) {
        requestThread_->signalThreadShouldExit();
        requestThread_->stopThread(3000);
        requestThread_.reset();
    }

    stopTimer();
    processing_ = false;

    appendToChat("[cancelled]\n");
    inputBox_.setEnabled(true);
    inputBox_.grabKeyboardFocus();
    restoreSendIcon();
}

void AIChatConsoleContent::restoreSendIcon() {
    auto enterSvg =
        juce::Drawable::createFromImageData(BinaryData::enter_svg, BinaryData::enter_svgSize);
    sendButton_.setImages(enterSvg.get());
    sendButton_.setAlpha(0.35f);
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

    if (activeTab_ == ConsoleTab::AI) {
        // Draw chat history + status footer as one rounded panel
        auto chatBounds = chatHistory_.getBounds().toFloat();
        auto statusBounds = configStatusLabel_.getBounds().toFloat();
        auto chatPanel = chatBounds.getUnion(statusBounds);
        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.fillRoundedRectangle(chatPanel, 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(chatPanel, 4.0f, 1.0f);

        // Separator between chat and status footer
        float sepY = chatBounds.getBottom();
        g.drawHorizontalLine(static_cast<int>(sepY), chatPanel.getX() + 1.0f,
                             chatPanel.getRight() - 1.0f);

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
                static const auto svgGrey = juce::Colour(0xFFB3B3B3);
                static const auto svgWhite = juce::Colours::white;
                auto iconCopy = icon->createCopy();
                iconCopy->replaceColour(svgGrey, colour);
                iconCopy->replaceColour(svgWhite, colour);
                iconCopy->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
            }
        }
    } else {
        // Draw DSL output area as rounded panel
        auto outputBounds = dslOutput_.getBounds().toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        g.fillRoundedRectangle(outputBounds, 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(outputBounds, 4.0f, 1.0f);
    }
}

void AIChatConsoleContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Tab buttons at bottom
    auto tabBar = bounds.removeFromBottom(22);
    aiTabButton_->setBounds(tabBar.removeFromLeft(28));
    tabBar.removeFromLeft(2);
    dslTabButton_->setBounds(tabBar.removeFromLeft(28));
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

        // Config status footer inside chat panel (bottom strip)
        int statusH = 26;

        chatHistory_.setBounds(bounds.withTrimmedBottom(statusH));

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

        // Status bar sits below chat history, inside the same visual panel
        auto statusBar = juce::Rectangle<int>(bounds.getX(), bounds.getBottom() - statusH,
                                              bounds.getWidth(), statusH);
        statusBar.reduce(6, 2);  // Padding
        if (serverToggleButton_ && serverToggleButton_->isVisible()) {
            statusBar.removeFromRight(2);
            serverToggleButton_->setBounds(statusBar.removeFromRight(20).reduced(1));
            serverToggleButton_->toFront(false);
        }
        configStatusLabel_.setBounds(statusBar);
        configStatusLabel_.toFront(false);
    } else {
        // DSL tab layout
        bounds.removeFromBottom(4);  // Spacing above status bar
        dslStatusLabel_.setBounds(bounds.removeFromBottom(20));
        bounds.removeFromBottom(2);  // Spacing above editor
        auto editorHeight = juce::jmax(60, bounds.getHeight() / 3);
        dslEditor_->setBounds(bounds.removeFromBottom(editorHeight));
        bounds.removeFromBottom(1);  // Separator
        dslOutput_.setBounds(bounds);
    }
}

void AIChatConsoleContent::onActivated() {
    buildAliasList();
    updateConfigStatus();
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
    aiTabButton_ =
        std::make_unique<magda::SvgButton>("AITab", BinaryData::ai_svg, BinaryData::ai_svgSize);
    aiTabButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    aiTabButton_->setActiveColor(juce::Colours::white);
    aiTabButton_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    aiTabButton_->setActiveBackgroundColor(DarkTheme::getAccentColour());
    aiTabButton_->setClickingTogglesState(true);
    aiTabButton_->setRadioGroupId(9001);
    aiTabButton_->setToggleState(true, juce::dontSendNotification);
    aiTabButton_->setTooltip("AI Chat");
    aiTabButton_->onClick = [this]() { switchTab(ConsoleTab::AI); };
    addAndMakeVisible(aiTabButton_.get());

    dslTabButton_ = std::make_unique<magda::SvgButton>("DSLTab", BinaryData::script_svg,
                                                       BinaryData::script_svgSize);
    dslTabButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    dslTabButton_->setActiveColor(juce::Colours::white);
    dslTabButton_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    dslTabButton_->setActiveBackgroundColor(DarkTheme::getAccentColour());
    dslTabButton_->setClickingTogglesState(true);
    dslTabButton_->setRadioGroupId(9001);
    dslTabButton_->setTooltip("DSL Console");
    dslTabButton_->onClick = [this]() { switchTab(ConsoleTab::DSL); };
    addAndMakeVisible(dslTabButton_.get());
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
    configStatusLabel_.setVisible(isAI);

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
    cancelRequest();
}

// ============================================================================
// ConfigListener
// ============================================================================

void AIChatConsoleContent::configChanged() {
    updateConfigStatus();
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

void AIChatConsoleContent::updateConfigStatus() {
    auto& config = magda::Config::getInstance();
    auto preset = config.getAIPreset();
    auto musicCfg = config.getAgentLLMConfig(magda::role::MUSIC);

    juce::String status;

    // Show preset/provider + model
    if (preset == "custom")
        status = "Custom";
    else {
        status = juce::String(preset).replaceCharacter('_', ' ');
        if (status.isNotEmpty())
            status = status.substring(0, 1).toUpperCase() + status.substring(1);
    }

    if (musicCfg.model.empty())
        status += " | Embedded";
    else
        status += " | " + juce::String(musicCfg.model);

    // If embedded local provider, show model status + toggle button
    if (isLocalPreset() && serverToggleButton_) {
        auto& mgr = magda::LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            auto modelName = juce::File(mgr.getLoadedModelPath()).getFileName();
            status += " | " + modelName;
            serverToggleButton_->updateSvgData(BinaryData::server_stop_svg,
                                               BinaryData::server_stop_svgSize);
            serverToggleButton_->setNormalColor(juce::Colours::limegreen);
            serverToggleButton_->setHoverColor(juce::Colours::limegreen.brighter(0.3f));
            serverToggleButton_->setVisible(true);
            configStatusLabel_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        } else {
            status += " | No model loaded";
            serverToggleButton_->updateSvgData(BinaryData::server_play_svg,
                                               BinaryData::server_play_svgSize);
            serverToggleButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
            serverToggleButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            serverToggleButton_->setVisible(true);
            configStatusLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
        }
        serverToggleButton_->repaint();
    } else {
        if (serverToggleButton_)
            serverToggleButton_->setVisible(false);
        configStatusLabel_.setColour(juce::Label::textColourId,
                                     DarkTheme::getSecondaryTextColour());
    }

    configStatusLabel_.setText(status, juce::dontSendNotification);
    resized();
}

bool AIChatConsoleContent::isLocalPreset() const {
    auto& config = magda::Config::getInstance();
    auto preset = config.getAIPreset();
    auto commandCfg = config.getAgentLLMConfig(magda::role::COMMAND);
    return commandCfg.provider == magda::provider::LLAMA_LOCAL ||
           preset == magda::preset::LOCAL_EMBEDDED || preset == "local";
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
