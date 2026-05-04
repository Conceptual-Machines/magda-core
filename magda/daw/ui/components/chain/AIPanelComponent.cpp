#include "AIPanelComponent.hpp"

#include <BinaryData.h>

#include "../../../../agents/llm_presets.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

class AIPanelComponent::GenerateThread : public juce::Thread {
  public:
    GenerateThread(AIPanelComponent& owner, std::unique_ptr<SoundDesignAgent> agent,
                   juce::String prompt, ChainNodePath path)
        : juce::Thread("MAGDA-SoundDesignAgent"),
          owner_(owner),
          agent_(std::move(agent)),
          prompt_(std::move(prompt)),
          path_(path) {}

    void run() override {
        auto safeOwner = juce::WeakReference<AIPanelComponent>(&owner_);
        if (threadShouldExit() || agent_ == nullptr) {
            postResult(safeOwner, "no agent");
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

        auto status = agent_->generateAndApply(prompt_, path_, std::move(onToken));
        if (threadShouldExit())
            return;
        postResult(safeOwner, status);
    }

    void cancel() {
        if (agent_)
            agent_->requestCancel();
        signalThreadShouldExit();
    }

  private:
    static void postResult(juce::WeakReference<AIPanelComponent> safeOwner, juce::String status) {
        juce::MessageManager::callAsync([safeOwner, status]() {
            if (auto* p = safeOwner.get())
                p->onGenerationFinished(status);
        });
    }

    AIPanelComponent& owner_;
    std::unique_ptr<SoundDesignAgent> agent_;
    juce::String prompt_;
    ChainNodePath path_;
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
    auto key = "\"" + field + "\":\"";
    int start = text.indexOf(key);
    if (start < 0)
        return {};
    start += key.length();
    int end = start;
    const int len = text.length();
    while (end < len) {
        auto c = text[end];
        if (c == '"' && (end == 0 || text[end - 1] != '\\'))
            break;
        ++end;
    }
    if (end >= len)
        return {};
    return text.substring(start, end);
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
    const bool supported = isSoundDesignSupported(pluginId_);
    input_.setEnabled(supported);
    if (supported) {
        input_.setTextToShowWhenEmpty(
            "describe the sound...", DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    } else {
        input_.setTextToShowWhenEmpty(
            "AI design not supported for this device",
            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY).withAlpha(0.4f));
    }
}

void AIPanelComponent::resized() {
    auto bounds = getLocalBounds().reduced(4);
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
}

void AIPanelComponent::submitPrompt() {
    auto prompt = input_.getText().trim();
    if (prompt.isEmpty())
        return;
    if (!isSoundDesignSupported(pluginId_)) {
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

    auto agent = createSoundDesignAgentFor(pluginId_);
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

    thread_ = std::make_unique<GenerateThread>(*this, std::move(agent), prompt, path_);
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

void AIPanelComponent::onGenerationFinished(juce::String status) {
    const bool succeeded = status.startsWith("applied");

    // On success, replace the streamed JSON dump with just the preset's
    // description — the param/wave/fx blocks are noise once the apply has
    // already run. On error / cancel keep the raw stream so the user can
    // see what came back. Falls back to the streamed text untouched if the
    // description field can't be located.
    if (succeeded && streamingStart_ >= 0 && streamingStart_ < output_.getText().length()) {
        auto streamed = output_.getText().substring(streamingStart_);
        auto description = extractStringField(streamed, "description");
        if (description.isNotEmpty()) {
            output_.setHighlightedRegion(
                juce::Range<int>(streamingStart_, output_.getText().length()));
            output_.setColour(juce::TextEditor::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            output_.insertTextAtCaret(description);
        }
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

    // Remind the user the preset is a starting point — only on a successful
    // apply (status messages from cancel / error / timeout shouldn't suggest
    // tweaking a preset that was never written). The disclaimer is rendered
    // in yellow so it doesn't drown in the streamed text above; the marker
    // string matches kDisclaimerMarker so restoreOutput can recolour it
    // after a slot rebuild.
    if (succeeded) {
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
}

void AIPanelComponent::refreshModelLabel() {
    auto cfg = Config::getInstance().getAgentLLMConfig(role::MUSIC);
    juce::String text =
        cfg.model.empty() ? juce::String("(no model configured)") : juce::String(cfg.model);
    modelLabel_.setText(text, juce::dontSendNotification);
}

}  // namespace magda::daw::ui
