#include "ai/AIPanelComponent.hpp"

#include <BinaryData.h>
#include <juce_llm/juce_llm.h>

#include "../../../../agents/llm_presets.hpp"
#include "../../../../agents/mcp/MCPServerManager.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class AIPanelComponent::GenerateThread : public juce::Thread {
  public:
    GenerateThread(AIPanelComponent& owner, std::unique_ptr<DeviceAIAgent> agent,
                   juce::String prompt, ChainNodePath path, llm::Conversation conversation)
        : juce::Thread("MAGDA-DeviceAIAgent"),
          owner_(owner),
          agent_(std::move(agent)),
          prompt_(std::move(prompt)),
          path_(path),
          conversation_(std::move(conversation)) {}

    void run() override {
        auto safeOwner = juce::WeakReference<AIPanelComponent>(&owner_);
        if (threadShouldExit() || agent_ == nullptr) {
            postResult(safeOwner, "no agent", {});
            return;
        }

        // Per-token forwarder — runs on this worker thread, hops each token
        // to the message thread before mutating the panel's text editor.
        auto onToken = [safeOwner, this](const juce::String& token) -> bool {
            if (threadShouldExit())
                return false;
            juce::MessageManager::callAsync([safeOwner, token]() {
                if (auto* p = safeOwner.get())
                    p->appendStreamingToken(token);
            });
            return true;
        };

        // conversation_ is this thread's own copy (loaded from the device on
        // submit). generateAndApply updates it in place; we serialise it back
        // on the message thread so the next turn continues from here.
        auto status = agent_->generateAndApply(prompt_, path_, conversation_, std::move(onToken));
        if (threadShouldExit())
            return;
        postResult(safeOwner, status, juce::JSON::toString(conversation_.toVar()));
    }

    void cancel() {
        if (agent_)
            agent_->requestCancel();
        signalThreadShouldExit();
    }

  private:
    static void postResult(juce::WeakReference<AIPanelComponent> safeOwner, juce::String status,
                           juce::String conversationJson) {
        juce::MessageManager::callAsync([safeOwner, status, conversationJson]() {
            if (auto* p = safeOwner.get())
                p->onGenerationFinished(status, conversationJson);
        });
    }

    AIPanelComponent& owner_;
    std::unique_ptr<DeviceAIAgent> agent_;
    juce::String prompt_;
    ChainNodePath path_;
    llm::Conversation conversation_;
};

AIPanelComponent::AIPanelComponent() {
    output_.setMultiLine(true, true);
    output_.setReadOnly(true);
    output_.setScrollbarsShown(true);
    output_.setCaretVisible(false);
    output_.setColour(juce::TextEditor::backgroundColourId,
                      DarkTheme::getColour(DarkTheme::BACKGROUND));
    output_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    output_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    output_.setColour(juce::TextEditor::textColourId,
                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    output_.setFont(FontManager::getInstance().getUIFont(11.0f));
    addAndMakeVisible(output_);

    input_.setMultiLine(false);
    input_.setReturnKeyStartsNewLine(false);
    input_.setTextToShowWhenEmpty("describe the sound...",
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    input_.setColour(juce::TextEditor::backgroundColourId,
                     DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    input_.setColour(juce::TextEditor::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    input_.setFont(FontManager::getInstance().getUIFont(11.0f));
    input_.onReturnKey = [this]() { submitPrompt(); };
    addAndMakeVisible(input_);

    // Footer model label — small, dim, left-aligned. The text gets refreshed
    // from Config every time the panel resizes / shows so a settings change
    // surfaces without a restart.
    modelLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    modelLabel_.setColour(juce::Label::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.5f));
    modelLabel_.setJustificationType(juce::Justification::centredLeft);
    modelLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(modelLabel_);

    auto deleteSvg =
        juce::Drawable::createFromImageData(BinaryData::delete_svg, BinaryData::delete_svgSize);
    clearButton_.setImages(deleteSvg.get());
    clearButton_.setEdgeIndent(2);
    clearButton_.setColour(juce::DrawableButton::backgroundColourId,
                           juce::Colours::transparentBlack);
    clearButton_.setColour(juce::DrawableButton::backgroundOnColourId,
                           juce::Colours::transparentBlack);
    clearButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    clearButton_.setTooltip("Clear chat");
    clearButton_.setAlpha(0.5f);
    clearButton_.onClick = [this]() { clearChat(); };
    addAndMakeVisible(clearButton_);

    // Faust MCP status strip (top). Visibility + content are set in
    // setDevicePluginId / updateMcpStatus once the device type is known; the
    // dot is painted in paint(). Hidden until then.
    mcpStatusLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    mcpStatusLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    mcpStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    mcpStatusLabel_.setInterceptsMouseClicks(false, false);
    mcpStatusLabel_.setVisible(false);
    addAndMakeVisible(mcpStatusLabel_);

    refreshModelLabel();
}

AIPanelComponent::~AIPanelComponent() {
    if (thread_) {
        thread_->cancel();
        thread_->stopThread(2000);
    }
}

namespace {
// Marker that begins the disclaimer line so we can locate it when restoring
// from the persisted output and recolour it yellow. Kept here so the writer
// (onGenerationFinished) and the restorer (setDevicePath) agree on the
// exact bytes — the leading "\n\n" is part of the marker so split logic
// works on the first occurrence with no ambiguity.
constexpr const char* kDisclaimerMarker = "\n\nnote: starting point only";

// Extract the value of a top-level JSON string field. The streamed content
// has had its curly braces replaced with spaces (see appendStreamingToken),
// but `"<field>":"<value>"` survives intact. Returns empty if not found or
// the closing quote is missing.
juce::String extractStringField(const juce::String& text, const juce::String& field) {
    // Tolerate whitespace around the colon: the model pretty-prints
    // `"description": "..."` (space after the colon), so a literal
    // `"field":"` probe misses it and the raw JSON would survive.
    auto key = "\"" + field + "\"";
    int i = text.indexOf(key);
    if (i < 0)
        return {};
    i += key.length();
    const int len = text.length();
    while (i < len && juce::CharacterFunctions::isWhitespace(text[i]))
        ++i;
    if (i >= len || text[i] != ':')
        return {};
    ++i;
    while (i < len && juce::CharacterFunctions::isWhitespace(text[i]))
        ++i;
    if (i >= len || text[i] != '"')
        return {};
    ++i;
    int start = i;
    while (i < len) {
        auto c = text[i];
        if (c == '"' && (i == 0 || text[i - 1] != '\\'))
            break;
        ++i;
    }
    if (i >= len)
        return {};
    return text.substring(start, i);
}
}  // namespace

void AIPanelComponent::setDevicePath(const ChainNodePath& path) {
    path_ = path;
    // Restore any prior output for this device — DeviceInfo persists across
    // DeviceSlotComponent rebuilds (which happen on notifyTrackDevicesChanged
    // for plugin loads, preset apply, sidechain edits, etc.), so the user's
    // streamed result and prompt history survive a slot teardown.
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_))
        restoreOutput(dev->aiPanelOutput);
}

void AIPanelComponent::restoreOutput(const juce::String& text) {
    // Re-insert the persisted text section-by-section so the disclaimer keeps
    // its yellow tint after a slot rebuild. setText would put everything in
    // one section using the current textColourId — recolouring afterwards is
    // not something juce::TextEditor supports per range, so we have to insert
    // the prefix and the disclaimer separately.
    output_.setText({}, juce::dontSendNotification);
    if (text.isEmpty())
        return;

    output_.moveCaretToEnd();
    const int markerIdx = text.indexOf(juce::String(kDisclaimerMarker));
    if (markerIdx < 0) {
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        output_.insertTextAtCaret(text);
    } else {
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        output_.insertTextAtCaret(text.substring(0, markerIdx));
        output_.setColour(juce::TextEditor::textColourId, juce::Colours::yellow);
        output_.insertTextAtCaret(text.substring(markerIdx));
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }
    output_.moveCaretToEnd();
}

void AIPanelComponent::setDevicePluginId(const juce::String& pluginId) {
    if (pluginId == pluginId_)
        return;
    pluginId_ = pluginId;
    const bool soundSupported = isSoundDesignSupported(pluginId_);
    const bool coderSupported = isCoderSupported(pluginId_);
    const bool supported = soundSupported || coderSupported;
    input_.setEnabled(supported);
    if (coderSupported) {
        input_.setTextToShowWhenEmpty(
            "describe an effect or instrument...",
            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    } else if (soundSupported) {
        input_.setTextToShowWhenEmpty(
            "describe the sound...", DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    } else {
        input_.setTextToShowWhenEmpty(
            "AI not supported for this device",
            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    }

    // The Faust MCP strip is only relevant to coder (Faust) devices — a 4OSC
    // panel doesn't touch faust-mcp, so it shouldn't advertise it.
    mcpStripVisible_ = coderSupported;
    mcpStatusLabel_.setVisible(mcpStripVisible_);
    updateMcpStatus();
    resized();
}

void AIPanelComponent::resized() {
    auto bounds = getLocalBounds().reduced(4);

    // Faust MCP status strip at the very top (coder devices only). Leaves a
    // gutter on the left for the dot painted in paint().
    if (mcpStripVisible_) {
        auto strip = bounds.removeFromTop(14);
        mcpStripBounds_ = strip;
        mcpStatusLabel_.setBounds(strip.withTrimmedLeft(14));
        bounds.removeFromTop(2);
    } else {
        mcpStripBounds_ = {};
    }

    // Footer strip at the very bottom: model label + clear-chat button.
    auto footerArea = bounds.removeFromBottom(16);
    constexpr int footerButtonSize = 16;
    clearButton_.setBounds(footerArea.removeFromRight(footerButtonSize));
    footerArea.removeFromRight(2);
    modelLabel_.setBounds(footerArea);

    bounds.removeFromBottom(2);
    auto inputArea = bounds.removeFromBottom(22);
    bounds.removeFromBottom(4);
    output_.setBounds(bounds);
    input_.setBounds(inputArea);

    refreshModelLabel();
}

void AIPanelComponent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    // NodeComponent paints a border around the panel strip, but our fillAll
    // above wipes the left / right / bottom edges of it. Redraw the border
    // here so the panel has a consistent outline on all four sides.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Faust MCP status light: dot at the left of the top strip. Grey when
    // disabled, green when enabled (brighter once actually connected).
    if (mcpStripVisible_ && !mcpStripBounds_.isEmpty()) {
        const float r = 6.0f;
        const float cx = static_cast<float>(mcpStripBounds_.getX()) + r * 0.5f + 2.0f;
        const float cy = static_cast<float>(mcpStripBounds_.getCentreY());
        const auto dot = !mcpEnabled_
                             ? DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.3f)
                             : (mcpRunning_ ? juce::Colours::limegreen
                                            : juce::Colours::limegreen.withAlpha(0.7f));
        g.setColour(dot);
        g.fillEllipse(cx - r * 0.5f, cy - r * 0.5f, r, r);
    }
}

void AIPanelComponent::updateMcpStatus() {
    auto& mgr = magda::MCPServerManager::getInstance();
    mcpEnabled_ = mgr.isServerEnabled("faust-mcp");
    mcpRunning_ = mgr.isServerRunning("faust-mcp");

    juce::String text;
    juce::Colour colour;
    if (!mcpEnabled_) {
        text = "Faust MCP off";
        colour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.5f);
    } else if (mcpRunning_) {
        text = "Faust MCP connected";
        colour = juce::Colours::limegreen;
    } else {
        text = "Faust MCP on";
        colour = juce::Colours::limegreen.withAlpha(0.85f);
    }

    mcpStatusLabel_.setText(text, juce::dontSendNotification);
    mcpStatusLabel_.setColour(juce::Label::textColourId, colour);
    repaint();
}

void AIPanelComponent::submitPrompt() {
    auto prompt = input_.getText().trim();
    if (prompt.isEmpty())
        return;
    if (!isDeviceAISupported(pluginId_)) {
        appendOutput("unsupported device");
        return;
    }

    // Cancel any in-flight generation before starting a new one — only one
    // active per panel; merging makes no sense for whole-preset generation.
    if (thread_) {
        thread_->cancel();
        thread_->stopThread(2000);
        thread_.reset();
    }

    appendOutput(juce::String::charToString(0x25CF) + " " + prompt);
    input_.clear();

    auto agent = createDeviceAIAgentFor(pluginId_);
    if (!agent) {
        appendOutput("no agent for " + pluginId_);
        return;
    }

    setBusy(true);

    // Make sure streaming starts on its own row. insertTextAtCaret keeps any
    // earlier coloured sections intact, where setText would flatten them.
    output_.moveCaretToEnd();
    auto current = output_.getText();
    if (current.isNotEmpty() && !current.endsWithChar('\n')) {
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        output_.insertTextAtCaret("\n");
    }
    streamingStart_ = output_.getText().length();
    persistOutput();

    // Load the running conversation from the device (message thread) so the
    // worker continues the multi-turn history. Empty / unparseable = fresh start.
    llm::Conversation conversation;
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_))
        conversation = llm::Conversation::fromVar(juce::JSON::parse(dev->aiConversation));

    thread_ = std::make_unique<GenerateThread>(*this, std::move(agent), prompt, path_,
                                               std::move(conversation));
    thread_->startThread();
}

void AIPanelComponent::appendStreamingToken(const juce::String& token) {
    // Strip JSON curly brackets so the streamed payload reads as content
    // rather than raw envelope syntax. Everything else (keys, values,
    // commas, quotes) flows through unchanged.
    auto cleaned = token.replaceCharacters("{}", "  ");
    // insertTextAtCaret is cheaper than full setText on every token, and
    // keeps the caret pinned to the end so the view auto-scrolls.
    output_.moveCaretToEnd();
    output_.insertTextAtCaret(cleaned);
    persistOutput();
}

void AIPanelComponent::onGenerationFinished(juce::String status, juce::String conversationJson) {
    const bool succeeded = status.startsWith("applied");

    // Persist the updated conversation onto the device (message thread) so the
    // next turn continues the history. Survives the slot rebuild below the same
    // way aiPanelOutput does.
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_))
        dev->aiConversation = conversationJson;

    // On success, replace the streamed JSON dump with just the preset's
    // description — the param/wave/fx blocks are noise once the apply has
    // already run. On error / cancel keep the raw stream so the user can
    // see what came back. Falls back to the streamed text untouched if the
    // description field can't be located.
    if (succeeded && streamingStart_ >= 0 && streamingStart_ < output_.getText().length()) {
        // Prefer the FINAL assistant turn from the conversation: after a retry
        // the stream holds several JSON blobs, and the first (failed) one's
        // description would be wrong. Fall back to the streamed text for agents
        // with no conversation (4OSC sound design).
        juce::String description;
        auto conv = llm::Conversation::fromVar(juce::JSON::parse(conversationJson));
        for (auto it = conv.messages.rbegin(); it != conv.messages.rend(); ++it) {
            if (it->role == "assistant") {
                description = extractStringField(it->content, "description");
                break;
            }
        }
        if (description.isEmpty())
            description =
                extractStringField(output_.getText().substring(streamingStart_), "description");
        if (description.isNotEmpty()) {
            output_.setHighlightedRegion(
                juce::Range<int>(streamingStart_, output_.getText().length()));
            output_.setColour(juce::TextEditor::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            output_.insertTextAtCaret(description);
        }
    }

    // For coder (Faust) devices, a successful result means the DSP passed the
    // MCP compile_faust check before it was applied. Surface that as a green
    // verification line; the "applied" line below confirms the live load.
    if (succeeded && isCoderSupported(pluginId_)) {
        output_.moveCaretToEnd();
        if (auto t = output_.getText(); t.isNotEmpty() && !t.endsWithChar('\n')) {
            output_.setColour(juce::TextEditor::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            output_.insertTextAtCaret("\n");
        }
        output_.setColour(juce::TextEditor::textColourId, juce::Colours::limegreen);
        output_.insertTextAtCaret(
            juce::String(juce::CharPointer_UTF8("\xe2\x9c\x93 compilation verified (MCP)")));
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }

    output_.moveCaretToEnd();
    auto current = output_.getText();
    if (current.isNotEmpty() && !current.endsWithChar('\n')) {
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        output_.insertTextAtCaret("\n");
    }
    output_.setColour(juce::TextEditor::textColourId,
                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    output_.insertTextAtCaret(juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 ")) + status);

    // Remind the user the preset is a starting point — only for sound-design
    // (4OSC) presets, and only on a successful apply (status messages from
    // cancel / error / timeout shouldn't suggest tweaking something that was
    // never written). Faust/coder devices get the "compiled" confirmation
    // above instead, so the disclaimer would be noise there. Rendered in
    // yellow; the marker matches kDisclaimerMarker so restoreOutput can
    // recolour it after a slot rebuild.
    if (succeeded && isSoundDesignSupported(pluginId_)) {
        output_.setColour(juce::TextEditor::textColourId, juce::Colours::yellow);
        // Wrap with CharPointer_UTF8 (matching the convention used for the
        // arrow above) so the em-dash byte sequence isn't decoded as Latin-1
        // by juce::String's const char* constructor and rendered as "â".
        output_.insertTextAtCaret(juce::String(juce::CharPointer_UTF8(
            "\n\nnote: starting point only \xe2\x80\x94 tweak by ear before saving.")));
        output_.setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    }
    output_.moveCaretToEnd();
    streamingStart_ = -1;
    setBusy(false);
    persistOutput();

    // A generation will have spawned faust-mcp via getServer(), so the strip
    // can now reflect the live "connected" state.
    updateMcpStatus();

    // Now that the final text is in place and persisted, fire the
    // tree-changed notification four_osc_apply intentionally skipped. The
    // resulting rebuild tears this panel down; the replacement panel reads
    // aiPanelOutput on setDevicePath and shows the same status + disclaimer.
    if (succeeded && path_.trackId != INVALID_TRACK_ID)
        TrackManager::getInstance().notifyTrackDevicesChanged(path_.trackId);
}

void AIPanelComponent::setBusy(bool busy) {
    input_.setEnabled(!busy);
}

void AIPanelComponent::appendOutput(const juce::String& line) {
    output_.moveCaretToEnd();
    output_.setColour(juce::TextEditor::textColourId,
                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    if (output_.getText().isNotEmpty())
        output_.insertTextAtCaret("\n");
    output_.insertTextAtCaret(line);
    output_.moveCaretToEnd();
    persistOutput();
}

void AIPanelComponent::persistOutput() {
    // Mirror the panel's text into DeviceInfo so a slot rebuild can restore
    // it. DeviceInfo lives on TrackManager and outlives the slot; the field
    // is transient (not serialized to disk).
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_))
        dev->aiPanelOutput = output_.getText();
}

void AIPanelComponent::clearChat() {
    output_.setText("", juce::dontSendNotification);
    persistOutput();
    // Reset the conversation too so the next prompt starts fresh.
    if (auto* dev = TrackManager::getInstance().getDeviceInChainByPath(path_))
        dev->aiConversation.clear();
}

void AIPanelComponent::refreshModelLabel() {
    auto cfg = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    juce::String text =
        cfg.model.empty() ? juce::String("(no model configured)") : juce::String(cfg.model);
    modelLabel_.setText(text, juce::dontSendNotification);
}

}  // namespace magda::daw::ui
